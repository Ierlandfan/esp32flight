#pragma once

#include <stdlib.h>

/* Desktop has one heap; the caps flags are decoration. */

#define MALLOC_CAP_SPIRAM   0
#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_8BIT     0
#define MALLOC_CAP_DMA      0

#define heap_caps_malloc(size, caps)              malloc(size)
#define heap_caps_calloc(n, size, caps)           calloc(n, size)
#define heap_caps_free(p)                         free(p)
#define heap_caps_realloc(p, size, caps)          realloc(p, size)
#define heap_caps_malloc_prefer(size, n, ...)     malloc(size)
#define heap_caps_realloc_prefer(p, size, n, ...) realloc(p, size)

#define heap_caps_get_free_size(caps)           (64u * 1024 * 1024)
#define heap_caps_get_total_size(caps)          (64u * 1024 * 1024)
#define heap_caps_get_largest_free_block(caps)  (64u * 1024 * 1024)
