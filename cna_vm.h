// src/crypto/cna_vm.h — CryptoNight-Adaptive v6 VM interface.
// Mirrors nerva-project/nerva: src/crypto/cna-vm.h (HF13 / v6).
// Pure C, no dependencies beyond stdint.
#ifndef CNA_VM_H
#define CNA_VM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* VM geometry — do not change without re-deriving the hash.           */
/* ------------------------------------------------------------------ */
#define CN_PROGRAM_SIZE   512
#define CN_REG_COUNT      8
#define CN_VM_ITERATIONS  2048

/* Scratchpad / salt sizes, bytes. v6 uses 8 MB scratchpad + 256 KB salt. */
#define CN_SCRATCHPAD_MEMORY_V13 (8u * 1024u * 1024u)
#define CN_SALT_MEMORY           (256u * 1024u)
#define CN_RANDOM_VALUES         64

/* ------------------------------------------------------------------ */
/* Opcodes                                                             */
/* ------------------------------------------------------------------ */
typedef enum cn_vm_opcode {
    CN_OP_IADD_RS  = 0,  /* reg[dst] = reg[src] + (imm << shift)       */
    CN_OP_ISUB     = 1,  /* reg[dst] = reg[src] - (imm >> shift)       */
    CN_OP_IMUL     = 2,  /* reg[dst] = reg[src] * imm                  */
    CN_OP_IXOR     = 3,  /* reg[dst] = reg[src] ^ imm                  */
    CN_OP_IROR     = 4,  /* reg[dst] = ror64(reg[src], imm & 63)       */
    CN_OP_CBRANCH  = 5,  /* if (reg[dst] & 1) pc += imm (else pc++)    */
    CN_OP_SP_READ  = 6,  /* reg[dst] = scratch[reg[src] & MASK]        */
    CN_OP_SP_WRITE = 7,  /* scratch[reg[dst] & MASK] = reg[src]        */
    CN_OP_MIX      = 8,  /* reg[dst] ^= scratch[reg[src] & MASK]       */
    CN_OP_COUNT    = 9
} cn_vm_opcode;

/* ------------------------------------------------------------------ */
/* One decoded VM instruction.                                         */
/* ------------------------------------------------------------------ */
typedef struct cn_vm_instruction {
    uint8_t  op;     /* cn_vm_opcode                               */
    uint8_t  dst;    /* destination register, 0..CN_REG_COUNT-1    */
    uint8_t  src;    /* source register,      0..CN_REG_COUNT-1    */
    uint8_t  shift;  /* shift / rotate amount, semantics per op    */
    uint32_t imm;    /* immediate operand                          */
} cn_vm_instruction;

/* ------------------------------------------------------------------ */
/* A full program: 512 instructions, generated per chain seed.         */
/* ------------------------------------------------------------------ */
typedef struct cn_vm_program {
    cn_vm_instruction instructions[CN_PROGRAM_SIZE];
} cn_vm_program;

/* ------------------------------------------------------------------ */
/* Per-thread random-value table. Generated once per miner thread      */
/* so each thread derives a different VM program from the same         */
/* chain seed. Zeroed for solo mining against a single thread.         */
/* ------------------------------------------------------------------ */
typedef struct cn_random_values {
    uint64_t v[CN_RANDOM_VALUES];
} cn_random_values;

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

/* Deterministically fill a random-values table from a 64-bit seed.
 * seed is typically (thread_id << 32) | chain_seed_lo.
 * Pass NULL rv for the single-threaded path — generator falls back
 * to a zeroed table. */
void cn_random_values_init(cn_random_values *rv, uint64_t seed);

/* Derive a CN_PROGRAM_SIZE-instruction program from a 32-byte seed.
 * Deterministic: same (seed, rv) always yields the same program.
 * rv may be NULL. */
void cn_vm_generate_program(cn_vm_program *prog,
                            const uint8_t seed[32],
                            const cn_random_values *rv);

/* Execute a program against an 8 MB scratchpad and an 8-entry register
 * file. regs[] is read and written in place. The caller owns the
 * scratchpad; it must be CN_SCRATCHPAD_MEMORY_V13 bytes, 64-byte
 * aligned. Program executes exactly CN_VM_ITERATIONS outer passes. */
void cn_vm_execute(const cn_vm_program *prog,
                   uint8_t *scratchpad,
                   uint64_t regs[CN_REG_COUNT]);

#ifdef __cplusplus
}
#endif

#endif /* CNA_VM_H */
