// language: C, file: stratum.c, target: Linux x86-64
#include "stratum.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define RXBUF 65536

struct stratum_ctx {
    int  fd;
    char rx[RXBUF];
    size_t rx_len;
    char session_id[80];
    pthread_mutex_t send_mtx;
    bool verbose;
};

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex2bin(const char *hex, uint8_t *out, size_t max_out) {
    size_t n = strlen(hex);
    if (n & 1 || n / 2 > max_out) return -1;
    for (size_t i = 0; i < n / 2; i++) {
        int h = hexval(hex[2*i]), l = hexval(hex[2*i+1]);
        if (h < 0 || l < 0) return -1;
        out[i] = (uint8_t)((h << 4) | l);
    }
    return (int)(n / 2);
}

static const char *json_find_key(const char *buf, const char *key) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(buf, needle);
    if (!p) return NULL;
    p = strchr(p + strlen(needle), ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static int json_string(const char *buf, const char *key, char *out, size_t out_sz) {
    const char *p = json_find_key(buf, key);
    if (!p || *p != '"') return -1;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_sz) out[i++] = *p++;
    if (*p != '"') return -1;
    out[i] = 0;
    return (int)i;
}

static long long json_int(const char *buf, const char *key) {
    const char *p = json_find_key(buf, key);
    if (!p) return -1;
    return strtoll(p, NULL, 10);
}

static int tcp_connect(const char *host, const char *port) {
    struct addrinfo hints = {0}, *res = NULL, *rp;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) return -1;
    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static int parse_url(const char *url, char *host, size_t hsz, char *port, size_t psz) {
    const char *p = url;
    if (strncmp(p, "stratum+tcp://", 14) == 0) p += 14;
    else if (strncmp(p, "stratum://", 10) == 0) p += 10;
    const char *colon = strrchr(p, ':');
    if (!colon) return -1;
    size_t hl = colon - p;
    if (hl == 0 || hl >= hsz) return -1;
    memcpy(host, p, hl);
    host[hl] = 0;
    snprintf(port, psz, "%s", colon + 1);
    return 0;
}

static int send_all(stratum_ctx *c, const char *msg) {
    pthread_mutex_lock(&c->send_mtx);
    size_t len = strlen(msg);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(c->fd, msg + sent, len - sent, 0);
        if (n <= 0) { pthread_mutex_unlock(&c->send_mtx); return -1; }
        sent += (size_t)n;
    }
    pthread_mutex_unlock(&c->send_mtx);
    if (c->verbose) fprintf(stderr, "[stratum <-] %s", msg);
    return 0;
}

static int recv_line(stratum_ctx *c, char *out, size_t out_sz) {
    for (;;) {
        char *nl = memchr(c->rx, '\n', c->rx_len);
        if (nl) {
            size_t line_len = (size_t)(nl - c->rx);
            size_t copy = line_len < out_sz - 1 ? line_len : out_sz - 1;
            memcpy(out, c->rx, copy);
            out[copy] = 0;
            size_t consumed = line_len + 1;
            memmove(c->rx, c->rx + consumed, c->rx_len - consumed);
            c->rx_len -= consumed;
            return (int)copy;
        }
        if (c->rx_len >= RXBUF - 1) return -1;
        ssize_t n = recv(c->fd, c->rx + c->rx_len, RXBUF - 1 - c->rx_len, 0);
        if (n <= 0) return -1;
        c->rx_len += (size_t)n;
    }
}

static bool parse_job(const char *js, stratum_job *job) {
    char blob_hex[256] = {0};
    char target_hex[80] = {0};
    char seed[80] = {0};

    if (json_string(js, "blob",   blob_hex, sizeof(blob_hex)) < 0)          return false;
    if (json_string(js, "job_id", job->job_id, sizeof(job->job_id)) < 0)    return false;
    if (json_string(js, "target", target_hex, sizeof(target_hex)) < 0)      return false;
    long long h = json_int(js, "height");
    job->height = (h > 0) ? (uint64_t)h : 0;

    int blen = hex2bin(blob_hex, job->blob, sizeof(job->blob));
    if (blen < 0) return false;
    job->blob_len = (size_t)blen;

    memset(job->target, 0, 32);
    size_t tlen = strlen(target_hex);
    if (tlen == 8) {
        uint8_t t[4];
        if (hex2bin(target_hex, t, 4) != 4) return false;
        memcpy(job->target, t, 4);
    } else if (tlen == 64) {
        if (hex2bin(target_hex, job->target, 32) != 32) return false;
    } else {
        return false;
    }

    json_string(js, "seed_hash", seed, sizeof(seed));
    snprintf(job->seed_hash, sizeof(job->seed_hash), "%s", seed);
    return true;
}

stratum_ctx *stratum_connect(const char *url, const char *user, const char *pass) {
    char host[256], port[16];
    if (parse_url(url, host, sizeof(host), port, sizeof(port)) != 0) return NULL;

    int fd = tcp_connect(host, port);
    if (fd < 0) return NULL;

    stratum_ctx *c = (stratum_ctx*)calloc(1, sizeof(*c));
    if (!c) { close(fd); return NULL; }
    c->fd = fd;
    pthread_mutex_init(&c->send_mtx, NULL);

    char req[1024];
    snprintf(req, sizeof(req),
             "{\"id\":1,\"jsonrpc\":\"2.0\",\"method\":\"login\","
             "\"params\":{\"login\":\"%s\",\"pass\":\"%s\",\"agent\":\"xnv/1.0\"}}\n",
             user, pass);
    if (send_all(c, req) != 0) { stratum_disconnect(c); return NULL; }

    char line[RXBUF];
    if (recv_line(c, line, sizeof(line)) < 0) { stratum_disconnect(c); return NULL; }
    if (c->verbose) fprintf(stderr, "[stratum ->] %s\n", line);

    if (strstr(line, "\"error\"") && !strstr(line, "\"error\":null") && !strstr(line, "\"error\": null")) {
        fprintf(stderr, "login failed: %s\n", line);
        stratum_disconnect(c);
        return NULL;
    }

    if (json_string(line, "id", c->session_id, sizeof(c->session_id)) < 0)
        c->session_id[0] = 0;

    return c;
}

void stratum_disconnect(stratum_ctx *c) {
    if (!c) return;
    if (c->fd >= 0) close(c->fd);
    pthread_mutex_destroy(&c->send_mtx);
    free(c);
}

void stratum_set_verbose(stratum_ctx *c, bool v) { c->verbose = v; }

bool stratum_wait_job(stratum_ctx *c, stratum_job *job_out) {
    char line[RXBUF];
    for (;;) {
        if (recv_line(c, line, sizeof(line)) < 0) return false;
        if (c->verbose) fprintf(stderr, "[stratum ->] %s\n", line);

        if (strstr(line, "\"method\":\"job\"") || strstr(line, "\"method\": \"job\"")) {
            if (parse_job(line, job_out)) return true;
            continue;
        }
        if (strstr(line, "\"result\"") && strstr(line, "\"job\"")) {
            if (parse_job(line, job_out)) return true;
            continue;
        }
        if (strstr(line, "\"status\":\"OK\"") || strstr(line, "\"status\": \"OK\"")) {
            fprintf(stderr, "[share] accepted\n");
            continue;
        }
        if (strstr(line, "\"status\":\"ERROR\"") || strstr(line, "\"status\": \"ERROR\"")) {
            fprintf(stderr, "[share] rejected: %s\n", line);
            continue;
        }
    }
}

bool stratum_submit(stratum_ctx *c, const stratum_job *job, uint32_t nonce, const uint8_t hash[32]) {
    char nonce_hex[16], result_hex[80];
    for (int i = 0; i < 4; i++)
        snprintf(nonce_hex + 2*i, 3, "%02x", ((uint8_t*)&nonce)[i]);
    nonce_hex[8] = 0;
    for (int i = 0; i < 32; i++)
        snprintf(result_hex + 2*i, 3, "%02x", hash[i]);
    result_hex[64] = 0;

    char req[512];
    static _Atomic int submit_id = 2;
    int id = submit_id++;
    snprintf(req, sizeof(req),
             "{\"id\":%d,\"jsonrpc\":\"2.0\",\"method\":\"submit\","
             "\"params\":{\"id\":\"%s\",\"job_id\":\"%s\",\"nonce\":\"%s\",\"result\":\"%s\"}}\n",
             id, c->session_id, job->job_id, nonce_hex, result_hex);
    return send_all(c, req) == 0;
}
