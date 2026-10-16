// language: C, file: verify_block.c, target: Linux x86-64
// Verify nerva_hash_v11 against a real chain block.
//
//   ./verify_block            # hashes the block at current_height - 10
//   ./verify_block 4443885    # hashes a specific height
//
// Prints MATCH or MISMATCH. MATCH means every stage of the pipeline is
// correct — Keccak absorb, init fill, memory loop, salt, implode, dispatch.
// MISMATCH means at least one parameter in nerva_hash.h is wrong, or the
// block hashing blob was reconstructed incorrectly.

#include "nerva_hash.h"
#include "daemon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int json_string(const char *buf, const char *key, char *out, size_t out_sz) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(buf, needle);
    if (!p) return -1;
    p = strchr(p + strlen(needle), ':');
    if (!p) return -1;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return -1;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_sz) out[i++] = *p++;
    if (*p != '"') return -1;
    out[i] = 0;
    return (int)i;
}

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

static void hexdump(const char *label, const uint8_t *data, size_t len) {
    printf("%s", label);
    for (size_t i = 0; i < len; i++) printf("%02x", data[i]);
    printf("\n");
}

// Varint reader for the block header parse.
static uint64_t read_varint(const uint8_t **p) {
    uint64_t v = 0;
    int shift = 0;
    while (**p & 0x80) { v |= (uint64_t)(**p & 0x7f) << shift; shift += 7; (*p)++; }
    v |= (uint64_t)(**p & 0x7f) << shift;
    (*p)++;
    return v;
}

int main(int argc, char **argv) {
    uint64_t height = 0;
    if (argc > 1) height = strtoull(argv[1], NULL, 10);

    if (!daemon_init("127.0.0.1", 17566)) {
        fprintf(stderr, "daemon_init failed\n");
        return 1;
    }

    if (height == 0) {
        uint64_t h = daemon_get_height();
        if (h == 0) { fprintf(stderr, "daemon unreachable\n"); return 1; }
        height = (h > 10) ? (h - 10) : h;
    }

    char body[256];
    snprintf(body, sizeof(body),
        "{\"jsonrpc\":\"2.0\",\"id\":\"0\",\"method\":\"get_block\","
        "\"params\":{\"height\":%llu}}", (unsigned long long)height);

    char resp[65536];
    if (daemon_rpc_call(body, resp, sizeof(resp)) != 0) {
        fprintf(stderr, "RPC failed\n");
        return 1;
    }

    char blob_hex[8192] = {0};
    char hash_hex[128]  = {0};
    char miner_hex[128] = {0};

    if (json_string(resp, "blob",          blob_hex,  sizeof(blob_hex))  < 0 ||
        json_string(resp, "hash",          hash_hex,  sizeof(hash_hex))  < 0 ||
        json_string(resp, "miner_tx_hash", miner_hex, sizeof(miner_hex)) < 0) {
        fprintf(stderr, "missing field in response\n");
        return 1;
    }

    uint8_t blob[4096], expected[32], miner_hash[32];
    int blen = hex2bin(blob_hex, blob, sizeof(blob));
    if (blen < 43) { fprintf(stderr, "blob too short (%d)\n", blen); return 1; }
    if (hex2bin(hash_hex,  expected,    32) != 32) return 1;
    if (hex2bin(miner_hex, miner_hash,  32) != 32) return 1;

    printf("Height   : %llu\n", (unsigned long long)height);
    printf("Blob len : %d\n", blen);

    // Block hashing blob: header(43) || miner_tx_hash(32) || varint(0x01)
    uint8_t hashing[128];
    memcpy(hashing, blob, 43);
    memcpy(hashing + 43, miner_hash, 32);
    hashing[75] = 0x01;
    size_t hashing_len = 76;

    hexdump("Hashing  : ", hashing, hashing_len);
    hexdump("Expected : ", expected, 32);

    uint8_t computed[32];
    nerva_hash_v11(hashing, hashing_len, computed,
                   XNV_V11_ITERS, XNV_V11_INIT_BLK, XNV_V11_XX, XNV_V11_YY);

    hexdump("Computed : ", computed, 32);
    printf("\n");

    if (memcmp(computed, expected, 32) == 0) {
        printf("MATCH — v11 parameters are correct. The miner will submit valid shares.\n");
        return 0;
    }

    printf("MISMATCH — the four XNV_V11_* values in nerva_hash.h are wrong.\n");
    for (int i = 0; i < 32; i++) {
        if (computed[i] != expected[i]) {
            printf("First differing byte at index %d\n", i);
            break;
        }
    }
    return 1;
}
