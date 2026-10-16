// language: C, file: main.c, target: Linux x86-64
// xnv — Nerva miner.
//
//   solo mode:  delegates to the daemon's start_mining. The daemon owns the
//               chain DB, so it can derive the per-block CNA seed that
//               cn_slow_hash_v11 needs. This is the only way an external
//               process produces valid Nerva blocks.
//
//   pool mode:  external hash path. Not currently functional for v11 (a
//               pool would have to deliver the seed with each job), but the
//               pipeline is preserved for future work.
#define _GNU_SOURCE
#include "cna.h"
#include "stratum.h"
#include "daemon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#define NONCE_OFFSET 39

struct miner_job {
    uint8_t  hashing_blob[256];
    size_t   hashing_len;
    uint8_t  target[32];
    uint64_t height;
    bool     solo;
    char     job_id[80];
};

static atomic_bool  g_stop       = false;
static atomic_ulong g_hashes     = 0;
static atomic_int   g_job_epoch  = 0;
static pthread_mutex_t g_job_mtx    = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_submit_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct miner_job g_job;

static stratum_ctx *g_pool   = NULL;
static bool         g_solo   = false;
static char         g_wallet[256];
static char         g_daemon_host[128] = "127.0.0.1";
static int          g_daemon_port      = 17566;

static void on_sigint(int sig) { (void)sig; atomic_store(&g_stop, true); }

static bool meets_target(const uint8_t hash[32], const uint8_t target[32]) {
    for (int i = 31; i >= 0; i--) {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return false;
}

static void job_from_stratum(const stratum_job *sj, struct miner_job *mj) {
    memset(mj, 0, sizeof(*mj));
    memcpy(mj->hashing_blob, sj->blob, sj->blob_len);
    mj->hashing_len = sj->blob_len;
    memcpy(mj->target, sj->target, 32);
    mj->height  = sj->height;
    mj->solo    = false;
    snprintf(mj->job_id, sizeof(mj->job_id), "%s", sj->job_id);
}

static void install_new_job(const struct miner_job *mj) {
    pthread_mutex_lock(&g_job_mtx);
    g_job = *mj;
    pthread_mutex_unlock(&g_job_mtx);
    atomic_fetch_add(&g_job_epoch, 1);
    fprintf(stderr, "[job] height=%llu %s\n",
            (unsigned long long)mj->height,
            mj->solo ? "(solo)" : mj->job_id);
}

// Reads /proc/meminfo and prints the hugepage pool the miner's next pad
// allocation will draw from. Solo mode delegates hashing to the daemon, so
// this reports the SYSTEM pool, not the daemon's own allocation. The daemon
// makes the same allocate_hugepage() call pool mode makes, so a pool of
// zero here means the daemon is hashing on 4 KB pages too — the tier the
// pad lands on is what actually carries the hashrate.
#if defined(__linux__)
static void report_hugepages(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;
    unsigned long total = 0, free_ = 0, size_kb = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        unsigned long v;
        if (sscanf(line, "HugePages_Total: %lu", &v) == 1) { total = v; continue; }
        if (sscanf(line, "HugePages_Free:  %lu", &v) == 1) { free_ = v; continue; }
        if (sscanf(line, "Hugepagesize:    %lu kB", &v) == 1) { size_kb = v; continue; }
    }
    fclose(f);

    if (total == 0) {
        fprintf(stderr,
            "[huge] no hugepages reserved — pad will fall back to THP/malloc; "
            "run ./hugepages.sh as root to fix\n");
    } else if (free_ == 0) {
        fprintf(stderr,
            "[huge] pool: %lu total / 0 free × %lu kB — exhausted, miner "
            "will fall through to THP\n",
            total, size_kb);
    } else {
        fprintf(stderr,
            "[huge] pool: %lu total / %lu free × %lu kB\n",
            total, free_, size_kb);
    }
}
#else
static void report_hugepages(void) {
    fprintf(stderr, "[huge] hugepage report only implemented for Linux\n");
}
#endif

// --- Pool mode: external hashing -------------------------------------------

struct worker_arg { int thread_id; int thread_count; };

static void *pool_worker(void *arg) {
    struct worker_arg *wa = (struct worker_arg*)arg;
    cna_ctx *ctx = cna_create(0);
    if (!ctx) {
        fprintf(stderr, "[t%d] ctx alloc failed\n", wa->thread_id);
        return NULL;
    }

    uint8_t hash[32];
    int seen_epoch = -1;
    struct miner_job job;

    while (!atomic_load(&g_stop)) {
        int epoch = atomic_load(&g_job_epoch);
        if (epoch != seen_epoch) {
            pthread_mutex_lock(&g_job_mtx);
            job = g_job;
            pthread_mutex_unlock(&g_job_mtx);
            cna_set_height(ctx, job.height);
            seen_epoch = epoch;
        }

        uint32_t nonce  = (uint32_t)wa->thread_id;
        uint32_t stride = (uint32_t)wa->thread_count;

        for (;;) {
            if (atomic_load(&g_stop)) break;
            if (atomic_load(&g_job_epoch) != seen_epoch) break;

            memcpy(job.hashing_blob + NONCE_OFFSET, &nonce, 4);
            cna_hash(ctx, job.hashing_blob, job.hashing_len, hash);

            atomic_fetch_add(&g_hashes, 1);

            if (meets_target(hash, job.target)) {
                fprintf(stderr, "[t%d] candidate nonce=%08x\n", wa->thread_id, nonce);
                pthread_mutex_lock(&g_submit_mtx);
                stratum_job sj;
                memset(&sj, 0, sizeof(sj));
                memcpy(sj.blob, job.hashing_blob, job.hashing_len);
                sj.blob_len = job.hashing_len;
                snprintf(sj.job_id, sizeof(sj.job_id), "%s", job.job_id);
                memcpy(sj.target, job.target, 32);
                sj.height = job.height;
                stratum_submit(g_pool, &sj, nonce, hash);
                pthread_mutex_unlock(&g_submit_mtx);
            }

            uint64_t next = (uint64_t)nonce + stride;
            nonce = (next > 0xFFFFFFFFULL) ? (uint32_t)wa->thread_id : (uint32_t)next;
        }
    }

    cna_destroy(ctx);
    return NULL;
}

static void *pool_reporter(void *arg) {
    (void)arg;
    unsigned long last = 0;
    while (!atomic_load(&g_stop)) {
        struct timespec ts = { .tv_sec = 10 };
        nanosleep(&ts, NULL);
        unsigned long now = atomic_load(&g_hashes);
        double hps = (now - last) / 10.0;
        last = now;
        fprintf(stderr, "[rate] %.2f H/s  total=%lu\n", hps, now);
    }
    return NULL;
}

// --- Solo mode: daemon-driven mining ---------------------------------------

// Polls the daemon every 5s. Reports three things: hashrate as the daemon
// sees it, height changes as the chain advances, and block-found deltas —
// the only "accepted share" solo mode has, since the daemon is the one doing
// the hashing and only surfaces a block once it has beaten the network
// target. First snapshot primes the counters so an already-running daemon
// does not fire a spurious [ACCEPT] on the first tick.
static void *daemon_rate_reporter(void *arg) {
    (void)arg;

    uint64_t last_blocks = 0;
    uint64_t last_height = 0;
    bool     seen_first  = false;
    struct timespec ts = { .tv_sec = 5 };

    while (!atomic_load(&g_stop)) {
        nanosleep(&ts, NULL);
        if (atomic_load(&g_stop)) break;

        daemon_mining_status_t st;
        if (!daemon_mining_status(&st)) {
            fprintf(stderr, "[rate] status RPC failed\n");
            continue;
        }

        if (!seen_first) {
            last_blocks = st.blocks_found;
            last_height = st.height;
            seen_first  = true;
        }

        if (st.blocks_found > last_blocks) {
            uint64_t delta = st.blocks_found - last_blocks;
            fprintf(stderr, "[ACCEPT] %llu block%s found — total=%llu\n",
                    (unsigned long long)delta,
                    delta == 1 ? "" : "s",
                    (unsigned long long)st.blocks_found);
        }
        last_blocks = st.blocks_found;

        if (st.height != 0 && st.height != last_height) {
            fprintf(stderr, "[chain] height %llu -> %llu\n",
                    (unsigned long long)last_height,
                    (unsigned long long)st.height);
            last_height = st.height;
        }

        if (!st.active && st.hashrate == 0) {
            fprintf(stderr, "[rate] daemon reports mining stopped\n");
        } else {
            fprintf(stderr,
                    "[rate] %llu H/s  threads=%d  active=%d  blocks=%llu  height=%llu\n",
                    (unsigned long long)st.hashrate, st.threads,
                    st.active ? 1 : 0,
                    (unsigned long long)st.blocks_found,
                    (unsigned long long)st.height);
        }
    }
    return NULL;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [solo|pool] -u USER [-p PASS] [-t N] [-v]\n"
        "  solo mode:  -s -d HOST:PORT -u WALLET\n"
        "  pool mode:  -a ALGO -o stratum+tcp://HOST:PORT -u USER -p PASS\n"
        "  -s        solo mining (daemon mines internally)\n"
        "  -d H:P    daemon RPC (default 127.0.0.1:17566)\n"
        "  -a ALGO   algorithm name\n"
        "  -o URL    pool url\n"
        "  -u USER   wallet / username\n"
        "  -p PASS   password (default x)\n"
        "  -t N      thread count\n",
        prog);
}

int main(int argc, char **argv) {
    const char *algo = NULL, *url = NULL, *user = NULL, *pass = "x";
    int threads = 0;

    int opt;
    while ((opt = getopt(argc, argv, "a:o:u:p:t:sd:vh")) != -1) {
        switch (opt) {
            case 'a': algo = optarg; break;
            case 'o': url  = optarg; break;
            case 'u': user = optarg; break;
            case 'p': pass = optarg; break;
            case 't': threads = atoi(optarg); break;
            case 's': g_solo = true; break;
            case 'd': {
                char buf[128];
                snprintf(buf, sizeof(buf), "%s", optarg);
                char *colon = strrchr(buf, ':');
                if (colon) {
                    *colon = 0;
                    snprintf(g_daemon_host, sizeof(g_daemon_host), "%s", buf);
                    g_daemon_port = atoi(colon + 1);
                } else {
                    snprintf(g_daemon_host, sizeof(g_daemon_host), "%s", buf);
                }
            } break;
            case 'v': break;
            case 'h':
            default:  usage(argv[0]); return 1;
        }
    }

    if (!user) { usage(argv[0]); return 1; }
    if (!g_solo && (!algo || !url)) { usage(argv[0]); return 1; }

    if (threads <= 0) {
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        threads = (n > 0) ? (int)n : 1;
    }

    snprintf(g_wallet, sizeof(g_wallet), "%s", user);

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);
    signal(SIGPIPE, SIG_IGN);

    // Report once before either mode spins up. The tier the pad lands on is
    // the same for the miner's threads (pool) and the daemon's threads
    // (solo), so this is worth seeing even when the daemon does the hashing.
    report_hugepages();

    if (g_solo) {
        fprintf(stderr, "xnv — Nerva miner (solo, daemon-driven)\n");
        fprintf(stderr, "user=%s  threads=%d  daemon=%s:%d\n",
                user, threads, g_daemon_host, g_daemon_port);

        if (!daemon_init(g_daemon_host, g_daemon_port)) {
            fprintf(stderr, "daemon init failed\n");
            return 1;
        }

        uint64_t h = daemon_get_height();
        if (h == 0) {
            fprintf(stderr, "daemon unreachable at %s:%d\n",
                    g_daemon_host, g_daemon_port);
            return 1;
        }
        fprintf(stderr, "daemon height=%llu\n", (unsigned long long)h);

        if (!daemon_start_mining(g_wallet, threads)) {
            fprintf(stderr, "daemon refused start_mining\n");
            return 1;
        }
        fprintf(stderr, "mining started on the daemon\n");

        pthread_t rep;
        pthread_create(&rep, NULL, daemon_rate_reporter, NULL);

        while (!atomic_load(&g_stop)) {
            struct timespec ts = { .tv_sec = 1 };
            nanosleep(&ts, NULL);
        }

        fprintf(stderr, "stopping daemon mining...\n");
        daemon_stop_mining();
        pthread_join(rep, NULL);
        daemon_cleanup();
        fprintf(stderr, "stopped.\n");
        return 0;
    }

    fprintf(stderr, "xnv — Nerva miner (pool)\n");
    fprintf(stderr, "user=%s  threads=%d  pool=%s\n", user, threads, url);

    g_pool = stratum_connect(url, user, pass);
    if (!g_pool) {
        fprintf(stderr, "failed to connect to pool\n");
        return 1;
    }

    stratum_job sj;
    if (!stratum_wait_job(g_pool, &sj)) {
        fprintf(stderr, "no initial job from pool\n");
        stratum_disconnect(g_pool);
        return 1;
    }
    struct miner_job mj;
    job_from_stratum(&sj, &mj);
    install_new_job(&mj);

    pthread_t *tids = calloc((size_t)threads, sizeof(pthread_t));
    struct worker_arg *wa = calloc((size_t)threads, sizeof(*wa));
    if (!tids || !wa) {
        stratum_disconnect(g_pool);
        free(tids); free(wa);
        return 1;
    }

    for (int i = 0; i < threads; i++) {
        wa[i].thread_id    = i;
        wa[i].thread_count = threads;
        pthread_create(&tids[i], NULL, pool_worker, &wa[i]);
    }

    pthread_t rep;
    pthread_create(&rep, NULL, pool_reporter, NULL);

    while (!atomic_load(&g_stop)) {
        if (!stratum_wait_job(g_pool, &sj)) {
            fprintf(stderr, "[stratum] connection lost\n");
            atomic_store(&g_stop, true);
            break;
        }
        job_from_stratum(&sj, &mj);
        install_new_job(&mj);
    }

    for (int i = 0; i < threads; i++) pthread_join(tids[i], NULL);
    pthread_join(rep, NULL);

    stratum_disconnect(g_pool);
    free(tids);
    free(wa);
    fprintf(stderr, "stopped. total hashes=%lu\n", atomic_load(&g_hashes));
    return 0;
}
