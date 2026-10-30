#include "vm.h"
#include "allocator.h"
#include "mmu.h"
#include "utils.h"

#define VM_TABLE_ENTRIES 512ULL
#define DESC_VALID       0x1ULL
#define DESC_TABLE       0x3ULL
#define DESC_BLOCK       0x1ULL
#define DESC_AF          (1ULL << 10)
#define DESC_AP_USER     (1ULL << 6)
#define DESC_AP_RO       (1ULL << 7)
#define DESC_ATTR(idx)   ((uint64_t)(idx) << 2)
#define DESC_PXN         (1ULL << 53)
#define DESC_UXN         (1ULL << 54)
#define DESC_COW         (1ULL << 55)
#define DESC_ADDR_MASK   0x0000FFFFFFFFF000ULL
#define DESC_FLAGS_MASK  (0xFFFULL | DESC_PXN | DESC_UXN)
#define VM_PMD_SIZE      (2ULL * 1024ULL * 1024ULL)
#define VM_PUD_SIZE      (1ULL << 30)
#define VM_MAIR_DEVICE   0U
#define VM_MAIR_NORMAL   1U

static uint64_t *alloc_table(void)
{
    uint64_t *table = (uint64_t *)kmalloc(VM_PAGE_SIZE);
    if (table)
        memset_(table, 0, VM_PAGE_SIZE);
    return table;
}

static uint64_t table_desc(const void *table)
{
    /* User page tables are allocated from the physical buddy allocator, so
     * their pointer value is already the physical address. */
    return ((uint64_t)(uintptr_t)table & DESC_ADDR_MASK) | DESC_TABLE;
}

static uint64_t *walk_pte(uint64_t *pgd, uint64_t va, int allocate)
{
    const unsigned int shifts[] = {39, 30, 21};
    uint64_t *table = pgd;

    for (unsigned int level = 0; level < 3; level++) {
        uint64_t index = (va >> shifts[level]) & 0x1FFULL;
        uint64_t *entry = &table[index];

        if ((*entry & DESC_VALID) == 0) {
            if (!allocate)
                return NULL;
            uint64_t *next = alloc_table();
            if (!next)
                return NULL;
            *entry = table_desc(next);
        } else if ((*entry & 0x3ULL) != DESC_TABLE) {
            /* Split an existing PUD/PMD block when a finer mapping is needed.
             * Preserve the old translation and attributes for every entry. */
            if (!allocate || (*entry & 0x3ULL) != DESC_BLOCK || level == 0)
                return NULL;

            uint64_t old_desc = *entry;
            uint64_t block_size = (level == 1) ? VM_PUD_SIZE : VM_PMD_SIZE;
            uint64_t next_block_size = block_size / VM_TABLE_ENTRIES;
            uint64_t block_base =
                (old_desc & DESC_ADDR_MASK) & ~(block_size - 1);
            uint64_t attrs = old_desc & DESC_FLAGS_MASK;
            uint64_t next_type = (level == 2) ? DESC_TABLE : DESC_BLOCK;
            uint64_t *next = alloc_table();
            if (!next)
                return NULL;

            for (uint64_t i = 0; i < VM_TABLE_ENTRIES; i++) {
                uint64_t pa = block_base + i * next_block_size;
                next[i] = (pa & DESC_ADDR_MASK) | attrs | next_type;
            }
            *entry = table_desc(next);
        }

        table = (uint64_t *)(uintptr_t)(*entry & DESC_ADDR_MASK);
    }

    return &table[(va >> 12) & 0x1FFULL];
}

static uint64_t user_page_desc(uint64_t pa, unsigned int flags)
{
    uint64_t desc = (pa & DESC_ADDR_MASK) | DESC_VALID | DESC_TABLE |
                    DESC_AF | DESC_ATTR((flags & VM_MAP_DEVICE)
                                         ? VM_MAIR_DEVICE : VM_MAIR_NORMAL);

    if (flags & VM_MAP_USER)
        desc |= DESC_AP_USER;
    if (!(flags & VM_MAP_WRITE))
        desc |= DESC_AP_RO;
    if (!(flags & VM_MAP_EXEC))
        desc |= DESC_PXN | DESC_UXN;
    else
        desc |= DESC_PXN;  /* EL0 may execute; EL1 executes from TTBR1. */
    return desc;
}

void *vm_create_user_address_space(void)
{
    uint64_t *pgd = alloc_table();
    uint64_t *pud = alloc_table();
    uint64_t *pmd = alloc_table();
    if (!pgd || !pud || !pmd) {
        if (pgd) kfree(pgd);
        if (pud) kfree(pud);
        if (pmd) kfree(pmd);
        return NULL;
    }

    pgd[0] = table_desc(pud);
    pud[0] = table_desc(pmd);

    /* Keep the kernel's existing low physical pointers usable at EL1.  These
     * identity blocks are kernel-only and non-executable.  The first 2 MB is
     * intentionally absent: VA 0 is where each process maps its own image. */
    const uint64_t end = (uint64_t)BUDDY_END;
    for (uint64_t pa = VM_PMD_SIZE; pa < end; pa += VM_PMD_SIZE) {
        uint64_t index = (pa >> 21) & 0x1FFULL;
        pmd[index] = (pa & DESC_ADDR_MASK) | DESC_BLOCK | DESC_AF |
                     DESC_ATTR(VM_MAIR_NORMAL) | DESC_PXN | DESC_UXN;
    }
    return pgd;
}

static void free_table_tree(uint64_t *table, unsigned int level)
{
    for (uint64_t i = 0; i < VM_TABLE_ENTRIES; i++) {
        uint64_t desc = table[i];
        if ((desc & DESC_VALID) == 0)
            continue;

        if (level == 3) {
            if ((desc & DESC_AP_USER) &&
                ((desc >> 2) & 0x7ULL) == VM_MAIR_NORMAL)
                kfree((void *)(uintptr_t)(desc & DESC_ADDR_MASK));
            continue;
        }

        if ((desc & 0x3ULL) == DESC_TABLE) {
            uint64_t *child = (uint64_t *)(uintptr_t)(desc & DESC_ADDR_MASK);
            free_table_tree(child, level + 1);
            kfree(child);
        }
    }
}

void vm_destroy_address_space(void *root)
{
    if (!root)
        return;
    uint64_t *pgd = (uint64_t *)root;
    free_table_tree(pgd, 0);
    kfree(pgd);
}

int vm_map_user_pages(void *root, uint64_t va, uint64_t pa,
                      uint64_t size, unsigned int flags)
{
    if (!root || size == 0 || (va & (VM_PAGE_SIZE - 1)) ||
        (pa & (VM_PAGE_SIZE - 1)) || (size & (VM_PAGE_SIZE - 1)) ||
        va >= (1ULL << 48) || size > (1ULL << 48) - va)
        return -1;

    uint64_t *pgd = (uint64_t *)root;
    for (uint64_t offset = 0; offset < size; offset += VM_PAGE_SIZE) {
        uint64_t *pte = walk_pte(pgd, va + offset, 1);
        if (!pte)
            return -1;
        if (*pte & DESC_VALID) {
            if (!(flags & VM_MAP_REPLACE) || !(flags & VM_MAP_DEVICE) ||
                !(flags & VM_MAP_USER))
                return -1;

            uint64_t old_pa = *pte & DESC_ADDR_MASK;
            unsigned int old_attr = (unsigned int)((*pte >> 2) & 0x7ULL);
            if (old_pa != ((pa + offset) & DESC_ADDR_MASK) ||
                ((*pte & DESC_AP_USER) && old_attr != VM_MAIR_DEVICE))
                return -1;
        }
        *pte = user_page_desc(pa + offset, flags);
    }
    asm volatile("dsb ishst" ::: "memory");
    asm volatile("tlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
    return 0;
}

int vm_map_user_buffer(void *root, uint64_t va, const void *src,
                       uint64_t size, unsigned int flags)
{
    if (!root || size == 0 || (va & (VM_PAGE_SIZE - 1)) ||
        va >= (1ULL << 48) || size > (1ULL << 48) - va ||
        size > (1ULL << 48) - VM_PAGE_SIZE)
        return -1;

    uint64_t mapped = 0;
    uint64_t page_count = (size + VM_PAGE_SIZE - 1) / VM_PAGE_SIZE;
    for (uint64_t i = 0; i < page_count; i++) {
        void *frame = kmalloc(VM_PAGE_SIZE);
        if (!frame)
            goto failed;
        memset_(frame, 0, VM_PAGE_SIZE);

        uint64_t offset = i * VM_PAGE_SIZE;
        if (src && offset < size) {
            uint64_t count = size - offset;
            if (count > VM_PAGE_SIZE)
                count = VM_PAGE_SIZE;
            memcpy_(frame, (const uint8_t *)src + offset, (size_t)count);
        }

        uint64_t *pte = walk_pte((uint64_t *)root, va + offset, 1);
        if (!pte || (*pte & DESC_VALID)) {
            kfree(frame);
            goto failed;
        }
        *pte = user_page_desc((uint64_t)(uintptr_t)frame, flags);
        mapped++;
    }

    asm volatile("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb"
                 ::: "memory");
    return 0;

failed:
    while (mapped) {
        mapped--;
        vm_unmap_user_page(root, va + mapped * VM_PAGE_SIZE);
    }
    return -1;
}

int vm_clone_user_mappings_cow(void *source_root, void *dest_root)
{
    if (!source_root || !dest_root)
        return -1;

    int parent_changed = 0;
    uint64_t *pgd = (uint64_t *)source_root;
    for (uint64_t l0 = 0; l0 < VM_TABLE_ENTRIES; l0++) {
        uint64_t e0 = pgd[l0];
        if ((e0 & 0x3ULL) != DESC_TABLE)
            continue;
        uint64_t *pud = (uint64_t *)(uintptr_t)(e0 & DESC_ADDR_MASK);

        for (uint64_t l1 = 0; l1 < VM_TABLE_ENTRIES; l1++) {
            uint64_t e1 = pud[l1];
            if ((e1 & 0x3ULL) != DESC_TABLE)
                continue;
            uint64_t *pmd = (uint64_t *)(uintptr_t)(e1 & DESC_ADDR_MASK);

            for (uint64_t l2 = 0; l2 < VM_TABLE_ENTRIES; l2++) {
                uint64_t e2 = pmd[l2];
                if ((e2 & 0x3ULL) != DESC_TABLE)
                    continue;
                uint64_t *pt = (uint64_t *)(uintptr_t)(e2 & DESC_ADDR_MASK);

                for (uint64_t l3 = 0; l3 < VM_TABLE_ENTRIES; l3++) {
                    uint64_t desc = pt[l3];
                    if ((desc & DESC_VALID) == 0 ||
                        (desc & DESC_AP_USER) == 0)
                        continue;

                    uint64_t va = (l0 << 39) | (l1 << 30) |
                                  (l2 << 21) | (l3 << 12);
                    uint64_t *child_pte = walk_pte((uint64_t *)dest_root,
                                                   va, 1);
                    if (!child_pte || (*child_pte & DESC_VALID))
                        goto failed;

                    uint64_t child_desc = desc;
                    if (((desc >> 2) & 0x7ULL) == VM_MAIR_NORMAL) {
                        void *frame = (void *)(uintptr_t)(desc & DESC_ADDR_MASK);
                        if (buddy_page_get(frame) != 0)
                            goto failed;
                        if ((desc & DESC_AP_RO) == 0) {
                            desc |= DESC_AP_RO | DESC_COW;
                            pt[l3] = desc;
                            child_desc = desc;
                            parent_changed = 1;
                        }
                    }
                    *child_pte = child_desc;
                }
            }
        }
    }
    if (parent_changed)
        asm volatile("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb"
                     ::: "memory");
    return 0;

failed:
    if (parent_changed)
        asm volatile("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb"
                     ::: "memory");
    return -1;
}

int vm_unmap_user_page(void *root, uint64_t va)
{
    if (!root || (va & (VM_PAGE_SIZE - 1)))
        return -1;
    uint64_t *pte = walk_pte((uint64_t *)root, va, 0);
    if (!pte || !(*pte & DESC_VALID))
        return -1;
    uint64_t desc = *pte;
    *pte = 0;
    asm volatile("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb"
                 ::: "memory");
    if ((desc & DESC_AP_USER) &&
        ((desc >> 2) & 0x7ULL) == VM_MAIR_NORMAL)
        kfree((void *)(uintptr_t)(desc & DESC_ADDR_MASK));
    return 0;
}

int vm_handle_cow_fault(void *root, uint64_t va)
{
    if (!root)
        return -1;
    uint64_t *pte = walk_pte((uint64_t *)root,
                             va & ~(VM_PAGE_SIZE - 1), 0);
    if (!pte || (*pte & (DESC_VALID | DESC_AP_USER | DESC_COW)) !=
                (DESC_VALID | DESC_AP_USER | DESC_COW) ||
        !(*pte & DESC_AP_RO) ||
        ((*pte >> 2) & 0x7ULL) != VM_MAIR_NORMAL)
        return -1;

    uint64_t old_desc = *pte;
    void *new_page = kmalloc(VM_PAGE_SIZE);
    if (!new_page)
        return -1;
    memcpy_(new_page, (const void *)(uintptr_t)(old_desc & DESC_ADDR_MASK),
            VM_PAGE_SIZE);

    uint64_t new_desc = (old_desc & ~(DESC_ADDR_MASK | DESC_AP_RO | DESC_COW)) |
                        ((uint64_t)(uintptr_t)new_page & DESC_ADDR_MASK);
    *pte = new_desc;
    asm volatile("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb"
                 ::: "memory");
    kfree((void *)(uintptr_t)(old_desc & DESC_ADDR_MASK));
    return 0;
}

/* Check the descriptor span at va. */
static int range_span_is_mapped(uint64_t *pgd, uint64_t va,
                                uint64_t *span_end)
{
    const unsigned int shifts[] = {39, 30, 21, 12};
    uint64_t *table = pgd;

    for (unsigned int level = 0; level < 4; level++) {
        uint64_t span = 1ULL << shifts[level];
        uint64_t *entry = &table[(va >> shifts[level]) & 0x1FFULL];
        *span_end = (va & ~(span - 1)) + span;

        if ((*entry & DESC_VALID) == 0)
            return 0;
        if (level == 3)
            return 1;

        if ((*entry & 0x3ULL) == DESC_TABLE) {
            table = (uint64_t *)(uintptr_t)(*entry & DESC_ADDR_MASK);
            continue;
        }

        /* Blocks and invalid descriptor forms are occupied. */
        return 1;
    }
    return 1;
}

int vm_user_range_is_free(void *root, uint64_t va, uint64_t size,
                          uint64_t *next_free_hint)
{
    const uint64_t va_limit = 1ULL << 48;
    if (!root || size == 0 || (va & (VM_PAGE_SIZE - 1)) ||
        (size & (VM_PAGE_SIZE - 1)) || va >= va_limit ||
        size > va_limit - va)
        return -1;

    uint64_t end = va + size;
    uint64_t cursor = va;
    uint64_t *pgd = (uint64_t *)root;
    while (cursor < end) {
        uint64_t span_end;
        if (range_span_is_mapped(pgd, cursor, &span_end)) {
            if (next_free_hint)
                *next_free_hint = span_end;
            return -1;
        }
        if (span_end <= cursor)
            return -1;
        cursor = span_end < end ? span_end : end;
    }

    if (next_free_hint)
        *next_free_hint = end;
    return 0;
}

int vm_translate_user(void *root, uint64_t va, uint64_t *pa,
                      int write, int execute)
{
    if (!root || !pa || va >= (1ULL << 48))
        return -1;
    uint64_t *pte = walk_pte((uint64_t *)root, va, 0);
    if (!pte || !(*pte & DESC_VALID) || !(*pte & DESC_AP_USER))
        return -1;
    if (write && (*pte & DESC_AP_RO))
        return -1;
    if (execute && (*pte & DESC_UXN))
        return -1;
    *pa = (*pte & DESC_ADDR_MASK) | (va & (VM_PAGE_SIZE - 1));
    return 0;
}

int vm_copy_from_user(void *root, void *dst, uint64_t src, size_t size)
{
    uint8_t *out = (uint8_t *)dst;
    while (size) {
        uint64_t pa;
        if (vm_translate_user(root, src, &pa, 0, 0) != 0)
            return -1;
        size_t chunk = (size_t)(VM_PAGE_SIZE - (src & (VM_PAGE_SIZE - 1)));
        if (chunk > size)
            chunk = size;
        memcpy_(out, (const void *)(uintptr_t)pa, chunk);
        out += chunk;
        src += chunk;
        size -= chunk;
    }
    return 0;
}

int vm_copy_to_user(void *root, uint64_t dst, const void *src, size_t size)
{
    const uint8_t *in = (const uint8_t *)src;
    while (size) {
        uint64_t pa;
        if (vm_translate_user(root, dst, &pa, 1, 0) != 0) {
            if (vm_handle_cow_fault(root, dst) != 0 ||
                vm_translate_user(root, dst, &pa, 1, 0) != 0)
                return -1;
        }
        size_t chunk = (size_t)(VM_PAGE_SIZE - (dst & (VM_PAGE_SIZE - 1)));
        if (chunk > size)
            chunk = size;
        memcpy_((void *)(uintptr_t)pa, in, chunk);
        in += chunk;
        dst += chunk;
        size -= chunk;
    }
    return 0;
}
