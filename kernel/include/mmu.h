#ifndef MMU_H
#define MMU_H

#include <stdint.h>
#include <memory.h>

#define MMU_PAGE_SIZE 4096UL
#define MMU_TABLE_ENTRIES 512UL

/* Physical pages used by boot.S before the allocator is available. */
#define BOOT_PGD_PA 0x1000UL
#define BOOT_PUD_PA 0x2000UL

/* Build the permanent TTBR1_EL1 linear mapping after the bootstrap mapping
 * has moved execution into the high-half kernel. */
void mmu_init_kernel_mapping(void);

void mmu_switch_ttbr0(uint64_t pgd_pa);
void mmu_switch_to_kernel_address_space(void);

#endif
