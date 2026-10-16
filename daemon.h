// language: C, file: daemon.h
// Nerva daemon RPC client.
//
// Two mining modes:
//   - template mode: fetch a block template, hash externally, submit. Works
//     for chains where the hash depends only on the blob. Not usable on
//     Nerva v11, which hashes with a chain-derived seed the daemon holds in
//     its own database.
//   - daemon mode: ask the daemon to mine internally. The daemon owns the
//     chain DB and can derive the CNA seed. This is the only way an external
//     process produces valid Nerva blocks.
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

// --- Connection -------------------------------------------------------------

bool     daemon_init(const char *host, int port);
void     daemon_cleanup(void);
uint64_t daemon_get_height(void);

// --- Template mode (external hashing) ---------------------------------------
//
// Kept for chains where the hash depends only on the blob. On Nerva v11
// these cannot produce a valid share.

bool     daemon_get_template(const char *wallet_address, daemon_job *job_out);
bool     daemon_submit_block(const uint8_t *template_blob, size_t template_len);

// --- Daemon mode (internal hashing) -----------------------------------------

// Snapshot of the daemon's own miner. hashrate is H/s as the daemon reports
// it. blocks_found is the count since the daemon started mining; height is
// best-effort from get_info and is 0 if the call failed. active is true when
// the daemon's response says mining is running — a false active with a stale
// hashrate means the daemon stopped the miner out from under us.
typedef struct {
    uint64_t hashrate;
    uint64_t blocks_found;
    uint64_t height;
    int      threads;
    bool     active;
} daemon_mining_status_t;

bool     daemon_start_mining(const char *wallet_address, int threads);
bool     daemon_mining_status(daemon_mining_status_t *out);
uint64_t daemon_mining_hashrate(void);
void     daemon_stop_mining(void);

// --- Raw RPC ----------------------------------------------------------------

int      daemon_rpc_call(const char *body, char *response, size_t resp_sz);

#endif
