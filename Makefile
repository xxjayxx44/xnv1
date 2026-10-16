# language: Makefile, file: Makefile, target: Linux x86-64, gcc/clang
# xnv — Nerva miner. Build for the machine you run on; do not distribute
# binaries built with -march=native across microarchitectures.
#
# The four wins, in order of ROI:
#   1. -march=native + -maes + -msse4.1 + -mavx2   -> AES-NI path compiles in
#   2. -flto + -O3                                 -> cross-TU inlining
#   3. run ./hugepages.sh once per boot            -> 2 MB pages for the pad
#   4. verify AES-NI actually emitted              -> make check-aes
#
# Verify the hardware path is live in the binary after building:
#   make check-aes
# If it prints aesenc>=1, the runtime dispatcher will use the HW path on a
# CPU that reports AES-NI. If it prints 0, -maes did not take and the SW
# path will run — 5–10x slower.

CC      ?= gcc
CSTD    := -std=c11
WARN    := -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
OPT     := -O3 -march=native -maes -msse4.1 -mavx2 -flto -fomit-frame-pointer \
           -funroll-loops -fno-plt -pipe
DEFS    := -D_GNU_SOURCE -DNDEBUG
INC     := -I.

# Files that must exist. slow-hash-hw.c and slow-hash-v8-hw.c hold the
# AES-NI translation units — if they are missing from your tree, comment
# them out and the build falls back to software AES via slow-hash.c's
# dispatch. The SW path is correct, just slow.
SRCS := \
    main.c \
    daemon.c \
    stratum.c \
    cna.c \
    cna-vm.c \
    nerva_hash.c \
    hash.c \
    hash-extra-blake.c \
    hash-extra-groestl.c \
    hash-extra-jh.c \
    hash-extra-skein.c \
    keccak.c \
    jh.c \
    skein.c \
    groestl.c \
    blake256.c \
    hc128.c \
    oaes_lib.c \
    aesb.c \
    memwipe.c \
    scratchpad.c \
    slow-hash.c \
    slow-hash-sw.c \
    slow-hash-hw.c \
    slow-hash-v8-sw.c \
    slow-hash-v8-hw.c

OBJS := $(SRCS:%.c=obj/%.o)
DEPS := $(OBJS:.o=.d)
BIN  := xnv

.PHONY: all clean check-aes check-huge run-solo

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OPT) -o $@ $^ $(LDFLAGS)

obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CSTD) $(WARN) $(OPT) $(DEFS) $(INC) -MMD -MP -c $< -o $@

-include $(DEPS)

# Confirm AES-NI intrinsics compiled into instructions, not into a library
# call. Expected: several hundred aesenc on any Haswell-and-later x86-64.
check-aes: $(BIN)
	@n=$$(objdump -d $(BIN) | grep -c aesenc); \
	 echo "aesenc instructions in $(BIN): $$n"; \
	 if [ "$$n" -lt 1 ]; then \
	   echo "WARNING: no aesenc emitted — hardware AES path not built."; \
	   echo "         The miner will fall back to software AES (~5-10x slower)."; \
	 else \
	   echo "OK: hardware AES path is compiled in."; \
	 fi

# Report whether the kernel has hugepages reserved for this miner.
check-huge:
	@grep -E 'HugePages_Total|HugePages_Free|Hugepagesize' /proc/meminfo || true
	@echo "If HugePages_Total is 0, run ./hugepages.sh (needs root)."

# Solo mode against a local daemon. WALLET=... make run-solo
run-solo: $(BIN)
	./$(BIN) -s -d 127.0.0.1:17566 -u $$WALLET

clean:
	rm -rf obj $(BIN)
