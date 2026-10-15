// language: C, file: scratchpad.c, target: Linux x86-64
#include "scratchpad.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef __linux__
#include <sys/mman.h>
#include <unistd.h>
#endif

#define HUGEPAGE_2MB (2u * 1024u * 1024u)

#ifndef MAP_HUGE_SHIFT
#define MAP_HUGE_SHIFT 26
#endif
#ifndef MAP_HUGE_2MB
#define MAP_HUGE_2MB (21 << MAP_HUGE_SHIFT)
#endif

static __thread sp_backing g_last_backing = SP_BACKING_FAILED;

int scratchpad_bind_node(int node) { (void)node; return -1; }
sp_backing scratchpad_last_backing(void) { return g_last_backing; }

static void *try_hugetlb(size_t size) {
#ifdef __linux__
    size_t rounded = (size + HUGEPAGE_2MB - 1) & ~(HUGEPAGE_2MB - 1);
    void *p = mmap(NULL, rounded, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB, -1, 0);
    if (p == MAP_FAILED) return NULL;
    memset(p, 0, rounded);
    return p;
#else
    (void)size; return NULL;
#endif
}

static void *try_thp(size_t size) {
    void *p = NULL;
    if (posix_memalign(&p, HUGEPAGE_2MB, size) != 0) return NULL;
#ifdef __linux__
    madvise(p, size, MADV_HUGEPAGE);
#endif
    memset(p, 0, size);
    return p;
}

static void *try_malloc(size_t size) {
    void *p = NULL;
    if (posix_memalign(&p, 64, size) != 0) return NULL;
    memset(p, 0, size);
    return p;
}

void *scratchpad_alloc(size_t size) {
    void *p;
    p = try_hugetlb(size);
    if (p) { g_last_backing = SP_BACKING_HUGETLB; return p; }
    p = try_thp(size);
    if (p) { g_last_backing = SP_BACKING_THP; return p; }
    p = try_malloc(size);
    if (p) { g_last_backing = SP_BACKING_MALLOC; return p; }
    g_last_backing = SP_BACKING_FAILED;
    return NULL;
}

void scratchpad_free(void *sp, size_t size) {
    if (!sp) return;
#ifdef __linux__
    if (g_last_backing == SP_BACKING_HUGETLB) {
        size_t rounded = (size + HUGEPAGE_2MB - 1) & ~(HUGEPAGE_2MB - 1);
        munmap(sp, rounded);
        return;
    }
#endif
    free(sp);
}
