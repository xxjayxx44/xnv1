// language: C, file: daemon.h
#ifndef DAEMON_H
#define DAEMON_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint8_t  hashing_blob[256];
    size_t   hashing_len;
    uint8_t  template_blob[512];
    size_t   template_len;
    uint64_t difficulty;
    uint64_t height;
    uint64_t reserved_offset;
    bool     valid;
} daemon_job;

bool     daemon_init(const char *host, int port);
bool     daemon_get_template(const char *wallet_address, daemon_job *job_out);
bool     daemon_submit_block(const uint8_t *template_blob, size_t template_len);
uint64_t daemon_get_height(void);
int      daemon_rpc_call(const char *body, char *response, size_t resp_sz);
void     daemon_cleanup(void);

#endif
