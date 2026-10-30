#include "syscall.h"
#include "allocator.h"
#include "vm.h"
#include "utils.h"

#define MMAP_PAGE_MASK (VM_PAGE_SIZE - 1)
#define USER_VA_LIMIT  (1ULL << 48)

static unsigned int mmap_page_flags(unsigned int prot)
{
    unsigned int flags = 0;
    if (prot != PROT_NONE)
        flags |= VM_MAP_USER;
    if (prot & PROT_WRITE)
        flags |= VM_MAP_WRITE;
    if (prot & PROT_EXEC)
        flags |= VM_MAP_EXEC;
    return flags;
}

static uint64_t align_mmap_address(uint64_t address)
{
    return (address + MMAP_PAGE_MASK) & ~MMAP_PAGE_MASK;
}

static int range_overlaps_region(thread_t *thread, uint64_t start,
                                 uint64_t length, uint64_t *next)
{
    uint64_t end = start + length;
    for (user_mmap_region_t *region = thread->mmap_regions;
         region; region = region->next) {
        uint64_t region_end = region->start + region->length;
        if (start < region_end && region->start < end) {
            if (next)
                *next = region_end;
            return 1;
        }
    }
    return 0;
}

static int range_is_available(thread_t *thread, uint64_t start,
                              uint64_t length, uint64_t *next)
{
    uint64_t vm_next = start + length;
    int vm_free = vm_user_range_is_free(thread->pagetable, start, length,
                                        &vm_next) == 0;
    uint64_t region_next = 0;
    int overlaps = range_overlaps_region(thread, start, length, &region_next);
    if (next) {
        *next = vm_next;
        if (overlaps && region_next > *next)
            *next = region_next;
    }
    return vm_free && !overlaps;
}

static int choose_mmap_address(thread_t *thread, uint64_t hint,
                               uint64_t length, uint64_t *chosen)
{
    uint64_t next;
    if (hint != 0 && !(hint & MMAP_PAGE_MASK) &&
        hint < VM_USER_MMAP_LIMIT && length <= VM_USER_MMAP_LIMIT - hint &&
        range_is_available(thread, hint, length, &next)) {
        *chosen = hint;
        return 0;
    }

    uint64_t candidate = thread->mmap_next_addr;
    if (candidate < VM_USER_MMAP_BASE)
        candidate = VM_USER_MMAP_BASE;
    candidate = align_mmap_address(candidate);

    while (candidate < VM_USER_MMAP_LIMIT &&
           length <= VM_USER_MMAP_LIMIT - candidate) {
        if (range_is_available(thread, candidate, length, &next)) {
            *chosen = candidate;
            return 0;
        }
        uint64_t advanced = align_mmap_address(next);
        if (advanced <= candidate)
            advanced = candidate + VM_PAGE_SIZE;
        candidate = advanced;
    }
    return -1;
}

user_mmap_region_t *vm_region_create(uint64_t start, uint64_t length,
                                     unsigned int prot, unsigned int flags,
                                     const void *backing,
                                     uint64_t backing_size)
{
    if (length == 0 || (start & MMAP_PAGE_MASK) ||
        (length & MMAP_PAGE_MASK) || start >= USER_VA_LIMIT ||
        length > USER_VA_LIMIT - start || backing_size > length ||
        (backing_size != 0 && !backing))
        return NULL;

    user_mmap_region_t *region =
        (user_mmap_region_t *)kmalloc(sizeof(*region));
    if (!region)
        return NULL;
    memset_(region, 0, sizeof(*region));
    region->start = start;
    region->length = length;
    region->page_count = length / VM_PAGE_SIZE;
    region->prot = prot;
    region->flags = flags;
    region->backing = (const uint8_t *)backing;
    region->backing_size = backing_size;
    return region;
}

void vm_region_list_free(user_mmap_region_t *regions)
{
    while (regions) {
        user_mmap_region_t *next = regions->next;
        kfree(regions);
        regions = next;
    }
}

uint64_t sys_mmap(uint64_t addr, uint64_t len, int prot, int flags,
                  int fd, uint64_t file_offset)
{
    thread_t *thread = (thread_t *)get_current();
    (void)fd;
    (void)file_offset;

    const int supported_flags = MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE;
    if (!thread || !thread->is_user || !thread->pagetable || len == 0 ||
        (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0 ||
        (flags & MAP_ANONYMOUS) == 0 || (flags & ~supported_flags) != 0 ||
        len > USER_VA_LIMIT - MMAP_PAGE_MASK)
        return (uint64_t)-1;

    uint64_t length = (len + MMAP_PAGE_MASK) & ~MMAP_PAGE_MASK;
    if (length == 0 || length > VM_USER_MMAP_LIMIT - VM_USER_MMAP_BASE)
        return (uint64_t)-1;

    uint64_t start;
    if (choose_mmap_address(thread, addr, length, &start) != 0)
        return (uint64_t)-1;

    user_mmap_region_t *region =
        vm_region_create(start, length, (unsigned int)prot,
                         (unsigned int)flags, NULL, 0);
    if (!region)
        return (uint64_t)-1;

    if ((flags & MAP_POPULATE) && prot != PROT_NONE &&
        vm_map_user_buffer(thread->pagetable, start, NULL, length,
                           mmap_page_flags((unsigned int)prot)) != 0) {
        kfree(region);
        return (uint64_t)-1;
    }

    region->next = thread->mmap_regions;
    thread->mmap_regions = region;
    if (thread->mmap_next_addr < start + length)
        thread->mmap_next_addr = start + length;
    return start;
}

int sys_mmap_fault(thread_t *thread, uint64_t address, int access)
{
    if (!thread || !thread->is_user || !thread->pagetable ||
        address >= USER_VA_LIMIT)
        return -1;

    user_mmap_region_t *region = thread->mmap_regions;
    while (region && (address < region->start ||
                      address >= region->start + region->length))
        region = region->next;
    if (!region)
        return -1;

    /* AP bits cannot make an EL0 page writable or executable without also
     * allowing data reads, so PROT_WRITE/PROT_EXEC mappings are readable here. */
    if ((access == MMAP_ACCESS_WRITE && !(region->prot & PROT_WRITE)) ||
        (access == MMAP_ACCESS_EXEC && !(region->prot & PROT_EXEC)) ||
        region->prot == PROT_NONE)
        return -1;

    uint64_t page = address & ~MMAP_PAGE_MASK;
    uint64_t pa;
    if (vm_translate_user(thread->pagetable, page, &pa, 0, 0) == 0) {
        if (access == MMAP_ACCESS_WRITE &&
            vm_translate_user(thread->pagetable, page, &pa, 1, 0) != 0)
            return vm_handle_cow_fault(thread->pagetable, page);
        if (access == MMAP_ACCESS_EXEC)
            return vm_translate_user(thread->pagetable, page, &pa, 0, 1);
        return 0;
    }

    uint64_t offset = page - region->start;
    const void *source = NULL;
    uint64_t source_size = VM_PAGE_SIZE;
    if (region->backing && offset < region->backing_size) {
        source = region->backing + offset;
        source_size = region->backing_size - offset;
        if (source_size > VM_PAGE_SIZE)
            source_size = VM_PAGE_SIZE;
    }
    return vm_map_user_buffer(thread->pagetable, page, source, source_size,
                              mmap_page_flags(region->prot));
}

int sys_mmap_clone(thread_t *source, thread_t *dest)
{
    if (!source || !dest)
        return -1;

    for (user_mmap_region_t *src = source->mmap_regions;
         src; src = src->next) {
        user_mmap_region_t *copy = vm_region_create(
            src->start, src->length, src->prot, src->flags,
            src->backing, src->backing_size);
        if (!copy)
            return -1;
        copy->next = dest->mmap_regions;
        dest->mmap_regions = copy;
    }
    dest->mmap_next_addr = source->mmap_next_addr;
    return 0;
}

void sys_mmap_release(thread_t *thread)
{
    if (!thread)
        return;
    user_mmap_region_t *region = thread->mmap_regions;
    thread->mmap_regions = NULL;
    vm_region_list_free(region);
    thread->mmap_next_addr = VM_USER_MMAP_BASE;
}
