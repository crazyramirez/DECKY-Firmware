#pragma once
#include <stdint.h>
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define MALLOC_CAP_DMA 8
static inline void *heap_caps_malloc(size_t n, uint32_t c) { (void)c; return malloc(n); }
static inline void *heap_caps_calloc(size_t a, size_t n, uint32_t c) { (void)c; return calloc(a, n); }
static inline void *heap_caps_aligned_alloc(size_t al, size_t n, uint32_t c) { (void)al; (void)c; return malloc(n); }
static inline void heap_caps_free(void *p) { free(p); }
static inline void *heap_caps_realloc(void *p, size_t n, uint32_t c) { (void)c; return realloc(p, n); }
static inline size_t heap_caps_get_free_size(uint32_t c) { return c & MALLOC_CAP_SPIRAM ? 7900000 : 187000; }
