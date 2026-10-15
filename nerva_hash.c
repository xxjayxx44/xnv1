// language: C, file: nerva_hash.c, target: Linux x86-64
// Wraps cn_hash_context_create and cn_slow_hash_v11 from the Nerva
// reference tree. One context per thread, allocated lazily.

#include "nerva_hash.h"
#include "hash-ops.h"

static __thread cn_hash_context_t *g_ctx = NULL;

static cn_hash_context_t *get_ctx(void) {
    if (!g_ctx) g_ctx = cn_hash_context_create();
    return g_ctx;
}

int nerva_hash_v11(const void *data, size_t len, uint8_t out[32],
                   size_t iters, uint8_t init_size_blk,
                   uint16_t xx, uint16_t yy) {
    cn_hash_context_t *ctx = get_ctx();
    if (!ctx) return -1;
    cn_slow_hash_v11(ctx, data, len, (char*)out,
                     iters, init_size_blk, xx, yy);
    return 0;
}

void nerva_hash_thread_cleanup(void) {
    if (g_ctx) {
        cn_hash_context_free(g_ctx);
        g_ctx = NULL;
    }
}
