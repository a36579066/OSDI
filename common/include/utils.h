#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

void align(void *size, unsigned int s);
uintptr_t align_up(uintptr_t x, unsigned int s);
uintptr_t align_down(uintptr_t x, unsigned int s);
int align_mem_offset(void* i, unsigned int align);
void *memcpy_(void *dest, const void *src, uintptr_t n);
void memset_(void *dest, int c, unsigned int count);

#endif