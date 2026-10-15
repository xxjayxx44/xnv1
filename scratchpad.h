// language: C, file: scratchpad.h
#ifndef SCRATCHPAD_H
#define SCRATCHPAD_H
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SP_BACKING_HUGETLB = 0,
    SP_BACKING_NUMA    = 1,
    SP_BACKING_THP     = 2,
    SP_BACKING_MALLOC  = 3,
    SP_BACKING_FAILED  = 4
} sp_backing;

void *scratchpad_alloc(size_t size);
void  scratchpad_free(void *sp, size_t size);
int   scratchpad_bind_node(int node);
sp_backing scratchpad_last_backing(void);

#endif
