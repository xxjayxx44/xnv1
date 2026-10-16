// language: C, file: daemon.h
// Nerva daemon RPC client.
//
// Two mining modes are supported through this interface:
//   - template mode: fetch a block template, hash externally, submit. This
//     works for chains where the hash depends only on the blob.
//   - daemon mode: ask the daemon to mine internally. This is the only mode
//     that works for Nerva, because v11 hashes with a chain-derived seed
//     that the daemon holds in its own database.
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
// Use these when the algorithm can be hashed from the blob alone. For Nerva
// v11 these cannot produce a valid share — see the daemon-mode block below.

bool     daemon_get_template(const char *wallet_address, daemon_job *job_out);
bool     daemon_submit_block(const uint8_t *template_blob, size_t template_len);

// --- Daemon mode (internal hashing) -----------------------------------------
//
// Ask the daemon to mine using its own threads. The daemon holds the
// blockchain database, so it can derive the per-block CNA seed that
// cn_slow_hash_v11 requires. This is the only way to mine Nerva from an
// external process, and it is what the wallet-miner front-ends use.
//
// daemon_start_mining sends { wallet_address, threads } to start_mining.
// Returns true if the daemon accepted the request.
//
// daemon_mining_hashrate polls mining_status and returns the current total
// hashrate in hashes per second (sum across the daemon's mining threads).
// Returns 0 on any error.
//
// daemon_stop_mining sends stop_mining. Safe to call when not mining.

bool     daemon_start_mining(const char *wallet_address, int threads);
uint64_t daemon_mining_hashrate(void);
void     daemon_stop_mining(void);

// --- Raw RPC ----------------------------------------------------------------

int      daemon_rpc_call(const char *body, char *response, size_t resp_sz);

#endif
