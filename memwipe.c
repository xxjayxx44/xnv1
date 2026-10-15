// language: C, file: memwipe.c
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "memwipe.h"

#define SCARECROW __asm__ __volatile__("" : : "r"(ptr) : "memory");

static void memory_cleanse(void *ptr, size_t len) {
    memset(ptr, 0, len);
    SCARECROW
}

void *memwipe(void *ptr, size_t n) {
    if (n > 0) memory_cleanse(ptr, n);
    SCARECROW
    return ptr;
}
