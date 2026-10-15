// language: C, file: main.c, target: Linux x86-64
#define _GNU_SOURCE
#include "cna.h"
#include "stratum.h"
#include "daemon.h"
#include "scratchpad.h"
#include "nerva_hash.h"
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
    uint8_t  template_blob[512];
    size_t   template_len;
    uint8_t  target[32];
    uint64_t height;
    uint64_t difficulty;
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
static uint64_t     g_last_height      = 0;

static void on_sigint(int sig) { (void)sig; atomic_store(&g_stop, true); }

static void difficulty_to_target(uint64_t diff, uint8_t target[32]) {
    if (diff == 0) { memset(target, 0, 32); return; }
    uint64_t limbs[4] = {~0ULL, ~0ULL, ~0ULL, ~0ULL};
    uint64_t rem = 0;
    for (int i = 3; i >= 0; i--) {
        __uint128_t cur = ((__uint128_t)rem << 64) | limbs[i];
        limbs[i] = (uint64_t)(cur / diff);
        rem      = (uint64_t)(cur % diff);
    }
    memcpy(target, limbs, 32);
}

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

static void job_from_daemon(const daemon_job *dj, struct miner_job *mj) {
    memset(mj, 0, sizeof(*mj));
    memcpy(mj->hashing_blob,  dj->hashing_blob,  dj->hashing_len);
    mj->hashing_len = dj->hashing_len;
    memcpy(mj->template_blob, dj->template_blob, dj->template_len);
    mj->template_len = dj->template_len;
    mj->height       = dj->height;
    mj->difficulty   = dj->difficulty;
    mj->solo         = true;
    difficulty_to_target(dj->difficulty, mj->target);
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

struct worker_arg { int thread_id; int thread_count; };

static void *worker(void *arg) {
    struct worker_arg *wa = (struct worker_arg*)arg;
    cna_ctx *ctx = cna_create(g_job.height);
    if (!ctx) { fprintf(stderr, "[t%d] ctx alloc failed\n", wa->thread_id); return NULL; }

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
            if (job.solo) memcpy(job.template_blob + NONCE_OFFSET, &nonce, 4);

            cna_hash(ctx, job.hashing_blob, job.hashing_len, hash);

            atomic_fetch_add(&g_hashes, 1);

            if (meets_target(hash, job.target)) {
                fprintf(stderr, "[t%d] candidate nonce=%08x\n", wa->thread_id, nonce);
                pthread_mutex_lock(&g_submit_mtx);
                if (job.solo) {
                    bool ok = daemon_submit_block(job.template_blob, job.template_len);
                    fprintf(stderr, "[block] %s nonce=%08x\n", ok ? "accepted" : "rejected", nonce);
                } else {
                    stratum_job sj;
                    memset(&sj, 0, sizeof(sj));
                    memcpy(sj.blob, job.hashing_blob, job.hashing_len);
                    sj.blob_len = job.hashing_len;
                    snprintf(sj.job_id, sizeof(sj.job_id), "%s", job.job_id);
                    memcpy(sj.target, job.target, 32);
                    sj.height = job.height;
                    stratum_submit(g_pool, &sj, nonce, hash);
                }
                pthread_mutex_unlock(&g_submit_mtx);
            }

            uint64_t next = (uint64_t)nonce + stride;
            nonce = (next > 0xFFFFFFFFULL) ? (uint32_t)wa->thread_id : (uint32_t)next;
        }
    }

    cna_destroy(ctx);
    return NULL;
}

static void *reporter(void *arg) {
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

static void *template_poller(void *arg) {
    (void)arg;
    while (!atomic_load(&g_stop)) {
        struct timespec ts = { .tv_sec = 5 };
        nanosleep(&ts, NULL);
        if (atomic_load(&g_stop)) break;
        uint64_t h = daemon_get_height();
        if (h > g_last_height) {
            daemon_job dj;
            if (daemon_get_template(g_wallet, &dj)) {
                struct miner_job mj;
                job_from_daemon(&dj, &mj);
                install_new_job(&mj);
                g_last_height = h;
            }
        }
    }
    return NULL;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [solo|pool] -u USER [-p PASS] [-t N] [-v]\n"
        "  solo mode:  -s -d HOST:PORT -u WALLET\n"
        "  pool mode:  -a ALGO -o stratum+tcp://HOST:PORT -u USER -p PASS\n"
        "  -s        solo mining\n"
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

    fprintf(stderr, "xnv — Nerva v11 miner (%s)\n", g_solo ? "solo" : "pool");
    fprintf(stderr, "user=%s  threads=%d\n", user, threads);

    pthread_t poller_tid = 0;
    bool      poller_started = false;

    if (g_solo) {
        fprintf(stderr, "daemon=%s:%d\n", g_daemon_host, g_daemon_port);
        if (!daemon_init(g_daemon_host, g_daemon_port)) return 1;
        uint64_t h = daemon_get_height();
        if (h == 0) { fprintf(stderr, "daemon unreachable\n"); return 1; }
        fprintf(stderr, "daemon height=%llu\n", (unsigned long long)h);
        g_last_height = h;

        daemon_job dj;
        if (!daemon_get_template(g_wallet, &dj)) {
            fprintf(stderr, "failed to fetch block template\n"); return 1;
        }
        struct miner_job mj;
        job_from_daemon(&dj, &mj);
        install_new_job(&mj);
        fprintf(stderr, "template hashing_len=%zu template_len=%zu diff=%llu\n",
                dj.hashing_len, dj.template_len, (unsigned long long)dj.difficulty);
    } else {
        g_pool = stratum_connect(url, user, pass);
        if (!g_pool) { fprintf(stderr, "failed to connect to pool\n"); return 1; }
        stratum_job sj;
        if (!stratum_wait_job(g_pool, &sj)) {
            fprintf(stderr, "no initial job\n");
            stratum_disconnect(g_pool); return 1;
        }
        struct miner_job mj;
        job_from_stratum(&sj, &mj);
        install_new_job(&mj);
    }

    pthread_t *tids = calloc((size_t)threads, sizeof(pthread_t));
    struct worker_arg *wa = calloc((size_t)threads, sizeof(*wa));
    if (!tids || !wa) return 1;

    for (int i = 0; i < threads; i++) {
        wa[i].thread_id    = i;
        wa[i].thread_count = threads;
        pthread_create(&tids[i], NULL, worker, &wa[i]);
    }

    pthread_t rep;
    pthread_create(&rep, NULL, reporter, NULL);

    if (g_solo) {
        pthread_create(&poller_tid, NULL, template_poller, NULL);
        poller_started = true;
    }

    if (g_solo) {
        while (!atomic_load(&g_stop)) {
            struct timespec ts = { .tv_sec = 1 };
            nanosleep(&ts, NULL);
        }
    } else {
        while (!atomic_load(&g_stop)) {
            stratum_job sj;
            if (!stratum_wait_job(g_pool, &sj)) {
                atomic_store(&g_stop, true); break;
            }
            struct miner_job mj;
            job_from_stratum(&sj, &mj);
            install_new_job(&mj);
        }
    }

    for (int i = 0; i < threads; i++) pthread_join(tids[i], NULL);
    pthread_join(rep, NULL);
    if (poller_started) pthread_join(poller_tid, NULL);

    if (g_pool) stratum_disconnect(g_pool);
    daemon_cleanup();
    free(tids);
    free(wa);

    fprintf(stderr, "stopped. total hashes=%lu\n", atomic_load(&g_hashes));
    return 0;
}
