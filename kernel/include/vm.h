#ifndef VM_H
#define VM_H

#include <stdint.h>
#include <type.h>

#define VM_PAGE_SIZE 4096ULL
#define VM_USER_CODE_BASE 0x0000000000000000ULL
#define VM_USER_CODE_LIMIT 0x00200000ULL
#define VM_USER_STACK_BASE 0x0000FFFFFFFFB000ULL
#define VM_USER_STACK_TOP  0x0000FFFFFFFFF000ULL
#define VM_USER_SIGNAL_TRAMPOLINE 0x0000FFFFFFFF9000ULL
#define VM_USER_SIGNAL_STACK 0x0000FFFFFFFFA000ULL
#define VM_USER_MMAP_BASE  0x0000000100000000ULL
#define VM_USER_MMAP_LIMIT VM_USER_SIGNAL_TRAMPOLINE

#define VM_MAP_USER   (1U << 0)
#define VM_MAP_WRITE  (1U << 1)
#define VM_MAP_EXEC   (1U << 2)
#define VM_MAP_DEVICE (1U << 3)
#define VM_MAP_REPLACE (1U << 4) /* Replace only an absent/kernel-only/device PTE. */

void *vm_create_user_address_space(void);
void vm_destroy_address_space(void *pgd);
int vm_map_user_pages(void *pgd, uint64_t va, uint64_t pa,
                      uint64_t size, unsigned int flags);
int vm_map_user_buffer(void *pgd, uint64_t va, const void *src,
                       uint64_t size, unsigned int flags);
int vm_unmap_user_page(void *pgd, uint64_t va);
/* Return 0 if free; otherwise set the next hint. */
int vm_user_range_is_free(void *pgd, uint64_t va, uint64_t size,
                          uint64_t *next_free_hint);
int vm_clone_user_mappings_cow(void *source_pgd, void *dest_pgd);
int vm_handle_cow_fault(void *pgd, uint64_t va);
int vm_translate_user(void *pgd, uint64_t va, uint64_t *pa,
                      int write, int execute);
int vm_copy_from_user(void *pgd, void *dst, uint64_t src, size_t size);
int vm_copy_to_user(void *pgd, uint64_t dst, const void *src, size_t size);

#endif
