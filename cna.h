// language: C, file: cna.h
#ifndef CNA_H
#define CNA_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct cna_ctx            cna_ctx;
typedef struct cna_midstate       cna_midstate;
typedef struct scratchpad_profile scratchpad_profile;

#define CNA_NONCE_OFFSET 39
#define CNA_PAD_SIZE     (1u * 1024u * 1024u)

cna_ctx *cna_create(uint64_t height);
cna_ctx *cna_create_strided(uint64_t height,
                            uint8_t *shared_pad, size_t shared_pad_size,
                            int thread_index, int thread_count);
void cna_destroy(cna_ctx *ctx);
void cna_set_height(cna_ctx *ctx, uint64_t height);
void cna_set_variant(cna_ctx *ctx, int variant);

void cna_hash(cna_ctx *ctx, const uint8_t *input, size_t inlen, uint8_t out[32]);

cna_midstate *cna_midstate_create(const uint8_t *blob, size_t blob_len);
void          cna_midstate_destroy(cna_midstate *ms);
void cna_hash_midstate(cna_ctx *ctx, const cna_midstate *ms,
                       uint32_t nonce, uint8_t out[32]);

void cna_set_profile(cna_ctx *ctx, const scratchpad_profile *prof);
void cna_set_nt_stores(cna_ctx *ctx, bool enable);

#endif
