// language: C, file: memwipe.h
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *memwipe(void *src, size_t n);

#ifdef __cplusplus
}
#endif
