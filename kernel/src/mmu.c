#include "mmu.h"
#include <allocator.h>
#include <gpio.h>
#include <utils.h>

#define PTE_VALID       0x1UL
#define PTE_TABLE       0x3UL
#define PTE_BLOCK       0x1UL
#define PTE_AF          (1UL << 10)
#define PTE_ATTR(idx)   ((uint64_t)(idx) << 2)

#define MAIR_DEVICE     0UL
#define MAIR_NORMAL     1UL

#define PUD_BLOCK_SIZE  (1UL << 30)
#define PMD_BLOCK_SIZE  (1UL << 21)

/* These tables are part of the kernel image, hence their backing physical
 * pages are reserved by the existing allocator along with _start.._end. */
static uint64_t kernel_pgd[MMU_TABLE_ENTRIES]
    __attribute__((aligned(MMU_PAGE_SIZE)));
static uint64_t kernel_pud[MMU_TABLE_ENTRIES]
    __attribute__((aligned(MMU_PAGE_SIZE)));
static uint64_t kernel_pmd[MMU_TABLE_ENTRIES]
    __attribute__((aligned(MMU_PAGE_SIZE)));
static uint64_t kernel_identity_pgd[MMU_TABLE_ENTRIES]
    __attribute__((aligned(MMU_PAGE_SIZE)));
static uint64_t kernel_identity_pud[MMU_TABLE_ENTRIES]
    __attribute__((aligned(MMU_PAGE_SIZE)));
static uint64_t kernel_identity_pmd[MMU_TABLE_ENTRIES]
    __attribute__((aligned(MMU_PAGE_SIZE)));
static uint64_t kernel_identity_pgd_pa;

static uint64_t table_descriptor(const void *table)
{
    return KVA_TO_PHYS(table) | PTE_TABLE;
}

static uint64_t block_descriptor(uint64_t pa, uint64_t attr_index)
{
    return pa | PTE_AF | PTE_ATTR(attr_index) | PTE_BLOCK;
}

void mmu_init_kernel_mapping(void)
{
    memset_(kernel_pgd, 0, sizeof(kernel_pgd));
    memset_(kernel_pud, 0, sizeof(kernel_pud));
    memset_(kernel_pmd, 0, sizeof(kernel_pmd));

    /* The first 1 GB contains both RAM and the RPi3 peripheral window.  Use
     * 2 MB PMD blocks so RAM becomes Normal memory while 0x3f000000 onward
     * remains Device memory. */
    kernel_pgd[0] = table_descriptor(kernel_pud);
    kernel_pud[0] = table_descriptor(kernel_pmd);
    for (uint64_t i = 0; i < MMU_TABLE_ENTRIES; i++) {
        uint64_t pa = i * PMD_BLOCK_SIZE;
        uint64_t attr = pa < PERIPHERAL_PHYS_BASE ? MAIR_NORMAL : MAIR_DEVICE;
        kernel_pmd[i] = block_descriptor(pa, attr);
    }

    /* ARM local peripherals occupy the next 1 GB and are entirely MMIO. */
    kernel_pud[1] = block_descriptor(PUD_BLOCK_SIZE, MAIR_DEVICE);

    /* TTBR0's kernel context preserves allocator-returned physical pointers
     * for EL1. User address spaces get their own copy plus user PTEs. */
    memset_(kernel_identity_pgd, 0, sizeof(kernel_identity_pgd));
    memset_(kernel_identity_pud, 0, sizeof(kernel_identity_pud));
    memset_(kernel_identity_pmd, 0, sizeof(kernel_identity_pmd));
    kernel_identity_pgd[0] = table_descriptor(kernel_identity_pud);
    kernel_identity_pud[0] = table_descriptor(kernel_identity_pmd);
    // The first 2MB memories are reversed by allocator, so we start from (2MB, ...]
    for (uint64_t pa = PMD_BLOCK_SIZE; pa < (uint64_t)BUDDY_END;
         pa += PMD_BLOCK_SIZE) {
        uint64_t index = (pa / PMD_BLOCK_SIZE) % MMU_TABLE_ENTRIES;
        kernel_identity_pmd[index] = block_descriptor(pa, MAIR_NORMAL) |
                                     (1UL << 53) | (1UL << 54);
    }
    kernel_identity_pgd_pa = KVA_TO_PHYS(kernel_identity_pgd);
    // uart_putint(kernel_identity_pgd_pa);
    /* The bootstrap TTBR1 table mapped the same range as Device memory.
     * Publish this finer table, then discard stale translations. */
    uint64_t pgd_pa = KVA_TO_PHYS(kernel_pgd);
    // uart_putint(pgd_pa);
    asm volatile("dsb ishst" ::: "memory");
    asm volatile("msr ttbr1_el1, %0" :: "r"(pgd_pa) : "memory");
    asm volatile("tlbi vmalle1is" ::: "memory");
    asm volatile("dsb ish\n\tisb" ::: "memory");
}

void mmu_switch_ttbr0(uint64_t pgd_pa)
{
    asm volatile("dsb ish" ::: "memory");
    asm volatile("msr ttbr0_el1, %0" :: "r"(pgd_pa) : "memory");
    asm volatile("tlbi vmalle1is" ::: "memory");
    asm volatile("dsb ish\n\tisb" ::: "memory");
}

void mmu_switch_to_kernel_address_space(void)
{
    mmu_switch_ttbr0(kernel_identity_pgd_pa);
}
