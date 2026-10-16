// language: C, file: daemon.c, target: Linux x86-64
#include "daemon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static char g_host[128];
static int  g_port;

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

static void bin2hex(const uint8_t *bin, size_t len, char *out) {
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[2*i]   = hx[bin[i] >> 4];
        out[2*i+1] = hx[bin[i] & 0xF];
    }
    out[2*len] = 0;
}

// The daemon exposes two RPC shapes:
//   - JSON-RPC 2.0 at /json_rpc: get_info, get_block_template, submit_block
//   - Legacy endpoints at their own paths: /start_mining, /mining_status,
//     /stop_mining. These take a plain JSON body, not a JSON-RPC envelope.
// rpc_call_ep takes the path so each caller sends its request to the right one.
static int rpc_call_ep(const char *path, const char *body, char *response, size_t resp_sz) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)g_port);
    if (inet_pton(AF_INET, g_host, &addr.sin_addr) != 1) {
        struct hostent *he = gethostbyname(g_host);
        if (!he) { close(fd); return -1; }
        memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
    }

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) { close(fd); return -1; }

    char req[65536];
    int req_len = snprintf(req, sizeof(req),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        path, g_host, g_port, strlen(body), body);
    if (req_len <= 0 || (size_t)req_len >= sizeof(req)) { close(fd); return -1; }
    if (send(fd, req, (size_t)req_len, 0) != req_len) { close(fd); return -1; }

    size_t total = 0;
    ssize_t n;
    while ((n = recv(fd, response + total, resp_sz - 1 - total, 0)) > 0) {
        total += (size_t)n;
        if (total >= resp_sz - 1) break;
    }
    close(fd);
    response[total] = 0;

    char *body_start = strstr(response, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        memmove(response, body_start, strlen(body_start) + 1);
    }
    return 0;
}

// JSON-RPC 2.0 methods go through /json_rpc.
static int rpc_call(const char *body, char *response, size_t resp_sz) {
    return rpc_call_ep("/json_rpc", body, response, resp_sz);
}

static const char *json_find(const char *buf, const char *key) {
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
    const char *p = json_find(buf, key);
    if (!p || *p != '"') return -1;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_sz) out[i++] = *p++;
    if (*p != '"') return -1;
    out[i] = 0;
    return (int)i;
}

static long long json_int(const char *buf, const char *key) {
    const char *p = json_find(buf, key);
    if (!p) return -1;
    return strtoll(p, NULL, 10);
}

bool daemon_init(const char *host, int port) {
    snprintf(g_host, sizeof(g_host), "%s", host);
    g_port = port;
    return true;
}

bool daemon_get_template(const char *wallet_address, daemon_job *job) {
    memset(job, 0, sizeof(*job));
    char body[1024];
    snprintf(body, sizeof(body),
        "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"get_block_template\","
        "\"params\":{\"wallet_address\":\"%s\",\"reserve_size\":16}}",
        wallet_address);
    char resp[32768];
    if (rpc_call(body, resp, sizeof(resp)) != 0) return false;

    char hashing_hex[1024]  = {0};
    char template_hex[2048] = {0};
    if (json_string(resp, "blockhashing_blob",  hashing_hex,  sizeof(hashing_hex))  < 0) return false;
    if (json_string(resp, "blocktemplate_blob", template_hex, sizeof(template_hex)) < 0) return false;

    int hl = hex2bin(hashing_hex, job->hashing_blob, sizeof(job->hashing_blob));
    if (hl < 0) return false;
    job->hashing_len = (size_t)hl;

    int tl = hex2bin(template_hex, job->template_blob, sizeof(job->template_blob));
    if (tl < 0) return false;
    job->template_len = (size_t)tl;

    job->difficulty      = (uint64_t)json_int(resp, "difficulty");
    job->height          = (uint64_t)json_int(resp, "height");
    job->reserved_offset = (uint64_t)json_int(resp, "reserved_offset");
    job->valid = (job->difficulty > 0 && job->hashing_len > 0 && job->template_len > 0);
    return job->valid;
}

bool daemon_submit_block(const uint8_t *template_blob, size_t template_len) {
    char hex[4096];
    bin2hex(template_blob, template_len, hex);
    char body[8192];
    snprintf(body, sizeof(body),
        "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"submit_block\","
        "\"params\":[\"%s\"]}",
        hex);
    char resp[4096];
    if (rpc_call(body, resp, sizeof(resp)) != 0) return false;
    return strstr(resp, "\"status\":\"OK\"") != NULL ||
           strstr(resp, "\"status\": \"OK\"") != NULL;
}

// --- Daemon-mode mining -----------------------------------------------------
//
// start_mining / mining_status / stop_mining live at their own HTTP paths,
// not under /json_rpc. They take a plain JSON body and return a plain JSON
// response. The daemon owns the chain DB and derives the CNA seed inside
// its own miner.

bool daemon_start_mining(const char *wallet_address, int threads) {
    if (threads <= 0) {
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        threads = (n > 0) ? (int)n : 1;
    }

    char body[1024];
    snprintf(body, sizeof(body),
        "{\"miner_address\":\"%s\",\"threads_count\":%d,"
        "\"do_background_mining\":false,\"ignore_battery\":true}",
        wallet_address, threads);

    char resp[8192];
    if (rpc_call_ep("/start_mining", body, resp, sizeof(resp)) != 0) {
        fprintf(stderr, "start_mining: RPC transport failed\n");
        return false;
    }

    // Success is {"status":"OK"}. Anything else, including a JSON "error"
    // object, is a refusal.
    if (!strstr(resp, "\"status\":\"OK\"") &&
        !strstr(resp, "\"status\": \"OK\"")) {
        fprintf(stderr, "start_mining rejected: %s\n", resp);
        return false;
    }
    return true;
}

uint64_t daemon_mining_hashrate(void) {
    char resp[16384];
    if (rpc_call_ep("/mining_status", "{}", resp, sizeof(resp)) != 0) return 0;

    long long speed = json_int(resp, "speed");
    if (speed < 0) return 0;
    return (uint64_t)speed;
}

void daemon_stop_mining(void) {
    char resp[4096];
    (void)rpc_call_ep("/stop_mining", "{}", resp, sizeof(resp));
}

// --- Raw RPC ----------------------------------------------------------------

int daemon_rpc_call(const char *body, char *response, size_t resp_sz) {
    return rpc_call(body, response, resp_sz);
}

void daemon_cleanup(void) {}
