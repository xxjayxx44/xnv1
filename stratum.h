// language: C, file: stratum.h
#ifndef STRATUM_H
#define STRATUM_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint8_t  blob[128];
    size_t   blob_len;
    char     job_id[80];
    uint8_t  target[32];
    uint64_t height;
    char     seed_hash[80];
} stratum_job;

typedef struct stratum_ctx stratum_ctx;

stratum_ctx *stratum_connect(const char *url, const char *user, const char *pass);
void         stratum_disconnect(stratum_ctx *ctx);
bool         stratum_wait_job(stratum_ctx *ctx, stratum_job *job_out);
bool         stratum_submit(stratum_ctx *ctx, const stratum_job *job,
                            uint32_t nonce, const uint8_t hash[32]);
void         stratum_set_verbose(stratum_ctx *ctx, bool v);

#endif
