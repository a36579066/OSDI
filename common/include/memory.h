#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

/* Lab 6 keeps the kernel's linear mapping in the upper 48-bit VA range. */
#define KERNEL_VA_BASE 0xffff000000000000ULL

#define PHYS_TO_KVA(pa) \
    ((uintptr_t)(pa) + (uintptr_t)KERNEL_VA_BASE)
#define KVA_TO_PHYS(va) \
    ((uintptr_t)(va) - (uintptr_t)KERNEL_VA_BASE)

#endif
