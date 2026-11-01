#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_INTERNAL 0
inline void *heap_caps_malloc(size_t n,int) {return std::malloc(n);}
inline size_t heap_caps_get_free_size(int) {return 1000000;}
