# language: Makefile, file: Makefile, target: Linux x86-64
# xnv — Nerva CNA v11 miner
#
#   make              # build
#   make clean

CC      = gcc
CFLAGS  = -O3 -maes -msse4.2 -march=native -pthread \
          -I. \
          -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
LDFLAGS = -pthread

SRCS = \
    main.c \
    nerva_hash.c \
    cna.c \
    stratum.c \
    daemon.c \
    scratchpad.c \
    memwipe.c \
    slow-hash.c \
    slow-hash-sw.c \
    slow-hash-v8-sw.c \
    cna-vm.c \
    oaes_lib.c \
    aesb.c \
    keccak.c \
    hash.c \
    hash-extra-blake.c \
    hash-extra-groestl.c \
    hash-extra-jh.c \
    hash-extra-skein.c \
    blake256.c \
    groestl.c \
    jh.c \
    skein.c

OBJS = $(SRCS:.c=.o)

xnv: $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c Makefile
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) xnv

.PHONY: clean
