// language: C, file: nerva_hash.h
// Thin C wrapper around Nerva's reference cn_slow_hash_v11.
//
// The fork-schedule parameters below are PLACEHOLDERS. They must be
// replaced with the values Nerva's daemon uses at your chain height, from
// cryptonote_basic_impl.cpp get_block_longhash(). Until then the hash
// runs but produces output that will not match the chain.
#ifndef NERVA_HASH_H
#define NERVA_HASH_H
#include <stdint.h>
#include <stddef.h>

// TODO: fill in from Nerva get_block_longhash at the current fork version.
#define XNV_V11_ITERS      1
#define XNV_V11_INIT_BLK   8
#define XNV_V11_XX         4
#define XNV_V11_YY         4

int  nerva_hash_v11(const void *data, size_t len, uint8_t out[32],
                    size_t iters, uint8_t init_size_blk,
                    uint16_t xx, uint16_t yy);

void nerva_hash_thread_cleanup(void);

#endif
