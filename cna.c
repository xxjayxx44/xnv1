// language: C, file: cna.c, target: Linux x86-64
// Miner-facing hash API mapped onto Nerva's reference cn_slow_hash_v11.
//
// The Monero CryptoNight reconstruction that used to live here is gone.
// This file is a shim: it keeps the cna_* interface that main.c expects,
// but routes every hash through nerva_hash_v11(). cna_set_variant,
// cna_set_profile, cna_set_nt_stores, and the midstate API become no-ops.

#include "cna.h"
#include "nerva_hash.h"
#include <stdlib.h>
#include <string.h>

struct cna_ctx            { uint64_t height; int variant; };
struct cna_midstate       { int unused; };
struct scratchpad_profile { int unused; };

cna_ctx *cna_create(uint64_t height) {
    struct cna_ctx *c = (struct cna_ctx*)calloc(1, sizeof(*c));
    if (c) c->height = height;
    return c;
}

cna_ctx *cna_create_strided(uint64_t height,
                            uint8_t *shared_pad, size_t shared_pad_size,
                            int thread_index, int thread_count) {
    (void)shared_pad; (void)shared_pad_size;
    (void)thread_index; (void)thread_count;
    return cna_create(height);
}

void cna_destroy(cna_ctx *c) { free(c); }

void cna_set_height(cna_ctx *c, uint64_t height) { c->height = height; }
void cna_set_variant(cna_ctx *c, int variant)    { c->variant = variant; }

void cna_hash(cna_ctx *c, const uint8_t *input, size_t inlen, uint8_t out[32]) {
    (void)c;
    nerva_hash_v11(input, inlen, out,
                   XNV_V11_ITERS, XNV_V11_INIT_BLK, XNV_V11_XX, XNV_V11_YY);
}

cna_midstate *cna_midstate_create(const uint8_t *blob, size_t blob_len) {
    (void)blob; (void)blob_len;
    return NULL;
}

void cna_midstate_destroy(cna_midstate *ms) { (void)ms; }

void cna_hash_midstate(cna_ctx *c, const cna_midstate *ms,
                       uint32_t nonce, uint8_t out[32]) {
    (void)ms; (void)nonce; (void)c; (void)out;
}

scratchpad_profile *scratchpad_profile_create(cna_ctx *ctx,
                                              const uint8_t *blob, size_t blob_len,
                                              uint32_t n_samples, uint32_t k_iters) {
    (void)ctx; (void)blob; (void)blob_len; (void)n_samples; (void)k_iters;
    return NULL;
}

void scratchpad_profile_destroy(scratchpad_profile *prof) { (void)prof; }

size_t scratchpad_profile_hot_count(const scratchpad_profile *prof) {
    (void)prof;
    return 0;
}

void cna_set_profile(cna_ctx *c, const scratchpad_profile *prof) {
    (void)c; (void)prof;
}

void cna_set_nt_stores(cna_ctx *c, bool enable) {
    (void)c; (void)enable;
}
