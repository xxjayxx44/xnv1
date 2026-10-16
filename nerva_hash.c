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

// Per-thread toggle. cn_nt_fill_enable() is a __thread variable inside the
// reference tree, so calling this once per worker thread (from the worker,
// not from main) is what arms the streaming stores for that thread's
// subsequent hashes.
void nerva_hash_set_nt_stores(int enable) {
    cn_nt_fill_enable(enable ? 1 : 0);
}

void nerva_hash_thread_cleanup(void) {
    if (g_ctx) {
        cn_hash_context_free(g_ctx);
        g_ctx = NULL;
    }
}
