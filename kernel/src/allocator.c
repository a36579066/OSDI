#include <allocator.h>
#include <utils.h>
#include <uart.h>
#include <exception_.h>
#include <device_tree.h>
#include <ramdisk.h>
#include <mmu.h>

#ifndef ALLOC_DEBUG
#define ALLOC_DEBUG 0
#endif

#ifndef ALLOC_DEMO
#define ALLOC_DEMO 0
#endif

#ifndef ALLOC_DEBUG_ALL
#define ALLOC_DEBUG_ALL 0
#endif

int DEBUG = ALLOC_DEBUG;
int DEMO = ALLOC_DEMO;
#define DEBUG_ALL ALLOC_DEBUG_ALL


static char* heap_ptr = &_heap_start;
int debug = 0;
extern char _start[];
extern char _end[];
extern char _kernel_start[];
/* ==================== for buddy system global variables ==================== */
buddy_system *buddy = 0;
buddy_block* node_metadata_cursor = 0;
chunk_page_t* chunk_page_metadata_cursor = 0;
/* ========================================================== */

/* ==================== for memory pool ==================== */
memory_pool_t mem_pools[NUM_POOLS];
/* ========================================================== */

/* ==================== for startup memory setting ==================== */
static uintptr_t metadata_cursor = BUDDY_METADATA_ADDR;
reserved_range_t reserved_ranges[MAX_RESERVED];
int reserved_count = 0;
/* ========================================================== */
/* ==================== some tools for buddy system ==================== */
static char* block_state_to_str(int status);
static char* node_availability_to_str(int availability);
static int buddy_first_page_for_order(int first_page, int order);
static int block_count_for_order(int order);
static uintptr_t block_size_for_order(int order);
static int node_index_for_order(int order, int local_block_index);
static buddy_block* parent_block(buddy_block* blk);
static void push_free_block(int order, buddy_block *blk);
static buddy_block* pop_free_block(int order);
static void remove_free_block(int order, buddy_block *blk);
static void set_node_availability(buddy_block* blk, int availability);
static int get_node_availability(buddy_block* blk);


static buddy_block* lookup_allocated_block(void* addr);
// void print_free_list();
// void print_alloc();
static buddy_block* block_for_page_and_order(int page_index, int order);
int in_chunk_freelist(memory_pool_t* m_pool, void* add);
int be_used(void* page_addr);
static int reserve_range(uintptr_t start, uintptr_t end);
/* ===================================================================== */

/* ==================== some tools for memory pool ==================== */
void print_pool_state();
int find_pool_index(size_t size);
/* ===================================================================== */

void* simple_malloc(size_t size) {
    irq_state_t state = irq_save();
    //  8-byte alignment
    align(&heap_ptr, 8); 
    char* start_addr = heap_ptr;
    
    if ((uintptr_t)heap_ptr + size > (uintptr_t)&_heap_end) {
        irq_restore(state);
        return NULL;  // not enough space
    }

    heap_ptr = (char *)((uintptr_t)heap_ptr + size);
    // uart_put_hex((uint32_t)(uintptr_t)heap_ptr);
    irq_restore(state);
    return start_addr;
}

void buddy_init() {
    // pool init
    for (int i = 0; i < NUM_POOLS; i++) {
        mem_pools[i].chunk_size = pool_sizes[i];
        mem_pools[i].free_list = NULL;
    }
    

    // 建立最上層 block
    buddy_block *root = buddy->node_table[0].block;
    root->first_page = 0;
    root->status = BUDDY_BLOCK_DIRECT;
    root->addr = (void *)BUDDY_START;
    root->size = BUDDY_ROOT_SIZE;
    root->prev = NULL;
    root->next = NULL;
    root->on_free_list = 0;
    set_node_availability(root, BUDDY_NODE_AVAILABLE);
    push_free_block(MAX_ORDER - 1, root);
    

    uart_puts("Buddy init complete with free list!\n\n");
    
    // 處理先前 reserved 的 region
    for(int i = 0; i < reserved_count; ++i){
        reserved_range_t range = reserved_ranges[i];
        if (reserve_range(range.start, range.end) != 0) {
            uart_puts("[buddy_init] failed to reserve a startup range\n");
        }
    }

    if(DEMO){
        uart_puts("init free_list: \n");
        print_free_list();
        print_alloc();
    }
    
}

void *buddy_malloc(unsigned int size, int chunk) {
    if (buddy == NULL || size == 0 || size > BUDDY_ROOT_SIZE) {
        if(DEMO)uart_puts("Error: Size out of range\n");
        return NULL;
    }

    int target_order = 0;
    uintptr_t total_size = PAGE_SIZE;
    while (total_size < size) {
        total_size <<= 1;
        target_order++;
    }
    
    if(DEBUG||DEMO){
        uart_puts("[malloc] Requested size = ");
        uart_putint(size);
        uart_puts(", target order = ");
        uart_putint(target_order);
        uart_puts("\n");
    }
    
    int order = target_order;
    
    
    // uart_put_hex((void*)buddy->free_lists[order]);
    // uart_puts("hi");
    while (order < MAX_ORDER && buddy->free_lists[order] == NULL){
        order++;  
    }
    
    if (order == MAX_ORDER) {
        if(DEMO)uart_puts("No available block\n");
        return NULL;
    }

    if(DEBUG){
        uart_puts("[malloc] Using block from order = ");
        uart_putint(order);
        uart_puts("\n");
    }
    
    
    buddy_block *blk = pop_free_block(order);
    if (blk == NULL) {
        return NULL;
    }

    set_node_availability(blk, BUDDY_NODE_IN_USE);

    while (order > target_order) {
        order--;

        buddy_block *left = block_for_page_and_order(blk->first_page, order);
        buddy_block *right = block_for_page_and_order(
            blk->first_page + (1 << order), order);
        if (left == NULL || right == NULL) {
            return NULL;
        }

        /* The old block is now an internal node; allocate its left child. */
        blk->status = BUDDY_BLOCK_SPLIT;
        set_node_availability(blk, BUDDY_NODE_IN_USE);
        blk->prev = NULL;
        blk->next = NULL;
        blk->on_free_list = 0;

        left->status = BUDDY_BLOCK_DIRECT;
        left->prev = NULL;
        left->next = NULL;
        left->on_free_list = 0;
        set_node_availability(left, BUDDY_NODE_IN_USE);

        right->status = BUDDY_BLOCK_DIRECT;
        right->prev = NULL;
        right->next = NULL;
        right->on_free_list = 0;
        set_node_availability(right, BUDDY_NODE_AVAILABLE);
        push_free_block(order, right);
        blk = left;
        
        if(DEBUG){
            uart_puts("[malloc] Split block →\n");
            uart_puts("  Left : addr = ");
            uart_put_hex((uintptr_t)blk->addr);
            uart_puts(", first_page = ");
            uart_putint(blk->first_page);
            uart_puts(", size = ");
            uart_putint(blk->size);
            uart_puts(", order = ");
            uart_putint(order);
            uart_puts(", buddy block status = ");
            uart_puts(block_state_to_str(right->status));
            uart_puts("\n");
            uart_puts("  Right: addr = ");
            uart_put_hex((uintptr_t)right->addr);
            uart_puts(", first_page = ");
            uart_putint(right->first_page);
            uart_puts(", size = ");
            uart_putint(right->size);
            uart_puts(", order = ");
            uart_putint(order);
            NL;
        }

    }
    if(chunk){
        blk->status = BUDDY_BLOCK_CHUNK_PAGE;
        
    } 
    else {
        blk->status = BUDDY_BLOCK_DIRECT;
        set_node_availability(blk, BUDDY_NODE_IN_USE);
        // buddy->node_table[blk->node_id].availability = BUDDY_NODE_IN_USE;
    }
    blk->refcount = target_order == 0 && !chunk ? 1U : 0U;

    
    if(DEBUG){
        uart_puts("[malloc] Allocated block: addr = ");
        uart_put_hex((uintptr_t)blk->addr);
        uart_puts(", size = ");
        uart_putint(blk->size);
        uart_puts(", first_page = ");
        uart_putint(blk->first_page);
        uart_puts(", order = ");
        uart_putint(__builtin_ctz(blk->size / PAGE_SIZE));
        uart_puts("\n");
        print_free_list();
        print_alloc();
        NL;
        NL;
    }
    if(DEMO) print_free_list();
    return blk->addr;
}

void *reserve_startup_allocations(unsigned int size, buddy_block* target_blk) {
    if (buddy == NULL || target_blk == NULL || size < PAGE_SIZE ||
        size > BUDDY_ROOT_SIZE) {
        uart_puts("Error: Size out of range\n");
        return NULL;
    }

    uintptr_t requested_size = size;
    if ((requested_size & (PAGE_SIZE - 1)) != 0 ||
        (requested_size & (requested_size - 1)) != 0) {
        uart_puts("Error: Reserved size must be a page-aligned power of two\n");
        return NULL;
    }

    int target_order = 0;
    uintptr_t block_size = PAGE_SIZE;
    while (block_size < requested_size) {
        block_size <<= 1;
        target_order++;
    }

    int target_page_idx = target_blk->first_page;
    if (target_page_idx < 0 || target_page_idx >= (int)BUDDY_PAGE_COUNT ||
        (target_page_idx & ((1 << target_order) - 1)) != 0 ||
        target_blk->size != PAGE_SIZE) {
        uart_puts("Error: Invalid startup allocation target\n");
        return NULL;
    }

    if (get_node_availability(target_blk) == BUDDY_NODE_IN_USE &&
        target_blk->status != BUDDY_BLOCK_SPLIT) {
        uart_puts("Error: addr already be used\n");
        return NULL;
    }

    if(DEBUG){
        uart_puts("[reserve] Requested size = ");
        uart_putint(size);
        uart_puts(", target order = ");
        uart_putint(target_order);
        uart_puts("\n");
    }

    /* Find the smallest free-list block that contains the target block. */
    int order = -1;
    buddy_block *blk = NULL;
    for (int candidate_order = MAX_ORDER - 1;
         candidate_order >= target_order; --candidate_order) {
        buddy_block *candidate = block_for_page_and_order(
            target_page_idx, candidate_order);
        if (candidate != NULL && candidate->status == BUDDY_BLOCK_DIRECT &&
            get_node_availability(candidate) == BUDDY_NODE_AVAILABLE &&
            candidate->on_free_list) {
            order = candidate_order;
            blk = candidate;
            break;
        }
    }

    if (blk == NULL || order < target_order) {
        uart_puts("No available block for startup reservation\n");
        return NULL;
    }
    remove_free_block(order, blk);
    if (blk->on_free_list) {
        uart_puts("No available block for startup reservation\n");
        return NULL;
    }

    if(DEBUG){
        uart_puts("[reserve] Using block from order = ");
        uart_putint(order);
        uart_puts("\n");
    }

    set_node_availability(blk, BUDDY_NODE_IN_USE);
    blk->status = (order == target_order) ? BUDDY_BLOCK_RESERVED : BUDDY_BLOCK_SPLIT;
    blk->prev = NULL;
    blk->next = NULL;
    blk->on_free_list = 0;

    while (order > target_order) {
        order--;

        buddy_block *left = block_for_page_and_order(blk->first_page, order);
        buddy_block *right = block_for_page_and_order(
            blk->first_page + (1 << order), order);
        if (left == NULL || right == NULL) {
            uart_puts("Error: Could not split startup reservation\n");
            return NULL;
        }

        int target_in_right = target_page_idx >= right->first_page;
        buddy_block *chosen = target_in_right ? right : left;
        buddy_block *sibling = target_in_right ? left : right;

        blk->status = BUDDY_BLOCK_SPLIT;
        set_node_availability(blk, BUDDY_NODE_IN_USE);
        blk->prev = NULL;
        blk->next = NULL;
        blk->on_free_list = 0;

        sibling->status = BUDDY_BLOCK_DIRECT;
        sibling->prev = NULL;
        sibling->next = NULL;
        sibling->on_free_list = 0;
        set_node_availability(sibling, BUDDY_NODE_AVAILABLE);
        push_free_block(order, sibling);

        chosen->prev = NULL;
        chosen->next = NULL;
        chosen->on_free_list = 0;
        set_node_availability(chosen, BUDDY_NODE_IN_USE);
        chosen->status = (order == target_order) ? BUDDY_BLOCK_RESERVED : BUDDY_BLOCK_SPLIT;
        blk = chosen;
        
        if(DEBUG){
            uart_puts("[malloc] Split block →\n");
            uart_puts("  main : addr = ");
            uart_put_hex((uintptr_t)blk->addr);
            uart_puts(", first_page = ");
            uart_putint(blk->first_page);
            uart_puts(", size = ");
            uart_putint(blk->size);
            uart_puts(", order = ");
            uart_putint(order);
            uart_puts(", buddy block status = ");
            uart_puts(block_state_to_str(sibling->status));
            uart_puts("\n");
            uart_puts("  buddy: addr = ");
            uart_put_hex((uintptr_t)sibling->addr);
            uart_puts(", first_page = ");
            uart_putint(sibling->first_page);
            uart_puts(", size = ");
            uart_putint(sibling->size);
            uart_puts(", order = ");
            uart_putint(order);
            uart_puts("\n");
        }

    }
    
    blk->status = BUDDY_BLOCK_RESERVED;
    set_node_availability(blk, BUDDY_NODE_IN_USE);
    return blk->addr;
}

void* test(int first_page){
    buddy_block* target_blk = block_for_page_and_order(first_page, 0);
    void* add = reserve_startup_allocations(PAGE_SIZE, target_blk);
    return add;
}

void buddy_free(void *addr) {
    if (addr == NULL) {
        return;
    }

    buddy_block *block = lookup_allocated_block(addr);
    if (block == NULL) {
        uart_puts("[free] ERROR: Unknown addr, cannot free\n");
        return;
    }

    if (block->addr != addr) {
        uart_puts("[free] ERROR: Address is not an allocation base\n");
        return;
    }

    if (block->status == BUDDY_BLOCK_RESERVED) {
        uart_puts("[free] ERROR: Cannot free reserved memory\n");
        return;
    }
    if (block->status != BUDDY_BLOCK_DIRECT && block->status != BUDDY_BLOCK_CHUNK_PAGE) {
        uart_puts("[free] ERROR: Address is not a directly allocated block\n");
        return;
    }

    int order = 0;
    unsigned int block_pages = block->size / PAGE_SIZE;
    while ((1U << order) < block_pages && order < MAX_ORDER) {
        order++;
    }
    if (order >= MAX_ORDER || (1U << order) != block_pages) {
        uart_puts("[free] ERROR: Invalid block metadata\n");
        return;
    }

    if (order == 0 && block->status == BUDDY_BLOCK_DIRECT &&
        block->refcount > 1) {
        block->refcount--;
        return;
    }
    block->refcount = 0;

    set_node_availability(block, BUDDY_NODE_AVAILABLE);
    block->status = BUDDY_BLOCK_DIRECT;
    block->prev = NULL;
    block->next = NULL;
    block->on_free_list = 0;

    if(DEBUG){
        uart_puts("[free] Start: addr = ");
        uart_put_hex((uintptr_t)addr);
        uart_puts(", first_page = ");
        uart_putint(block->first_page);
        uart_puts(", order = ");
        uart_putint(order);
        uart_puts("\n");
    }

    while (order < MAX_ORDER - 1) {
        int buddy_first_page =
            buddy_first_page_for_order(block->first_page, order);
        buddy_block *buddy_blk =
            block_for_page_and_order(buddy_first_page, order);

        if (buddy_blk == NULL || buddy_blk->status != BUDDY_BLOCK_DIRECT ||
            get_node_availability(buddy_blk) != BUDDY_NODE_AVAILABLE ||
            !buddy_blk->on_free_list) {
            if(DEBUG){
                uart_puts("[free] Cannot merge with buddy first_page = ");
                uart_putint(buddy_first_page);
                uart_puts(" at order ");
                uart_putint(order);
                uart_puts(", buddy_blk status: ");
                uart_puts(buddy_blk ? block_state_to_str(buddy_blk->status) : "NULL");
                uart_puts(", buddy_blk availability: ");
                uart_puts(buddy_blk ? node_availability_to_str(get_node_availability(buddy_blk)) : "NULL");
                uart_puts("\n");
            }
            break;
        }
        if(DEMO){
            uart_puts("[free] Merging with buddy first_page = ");
            uart_putint(buddy_first_page);
            uart_puts(" at order ");
            uart_putint(order);
            uart_puts(", buddy_blk status: ");
            uart_puts(block_state_to_str(buddy_blk->status));
            uart_puts(", buddy_blk addr: ");
            uart_put_hex((uintptr_t)buddy_blk->addr);
            NL;
        }
        
        buddy_block* parent_blk = parent_block(block);
        if (parent_blk == NULL) {
            break;
        }

        remove_free_block(order, buddy_blk);

        parent_blk->status = BUDDY_BLOCK_DIRECT;
        set_node_availability(parent_blk, BUDDY_NODE_AVAILABLE);
        parent_blk->prev = NULL;
        parent_blk->next = NULL;
        parent_blk->on_free_list = 0;
        block = parent_blk;
        order++;
    }

    push_free_block(order, block);

    if(DEBUG){
        uart_puts("[free] Final block: addr = ");
        uart_put_hex((uintptr_t)block->addr);
        uart_puts(", size = ");
        uart_putint(block->size);
        uart_puts(", order = ");
        uart_putint(order);
        uart_puts("\n");
        print_free_list();
        print_alloc();
        NL;
    }

}

void* kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    irq_state_t state = irq_save();
    int index = find_pool_index(size);
    memory_pool_t *pool = NULL;
    if (index == -1) {
        if(DEMO)
        uart_puts("[kmalloc] Size too large, use buddy\n");
        void* addr =  buddy_malloc(size, 0);
        irq_restore(state);
        return addr; // fallback
    }else {
        while(index < NUM_POOLS){
            pool = &mem_pools[index];
            
            if(!pool->free_list && pool->num_page >= MAX_PAGE_NUM_FOR_CHUNK){
                index++; 
                continue;
            }
            break;
        }
        if(index >= NUM_POOLS){ // chunk 用完了
            uart_puts("[kmalloc] Chunck used up, use buddy.\n");
            void *addr = buddy_malloc(size, 0);
            irq_restore(state);
            return addr;
        }
        if(DEMO){
            uart_puts("[kmalloc] Using memory pool index: ");
            uart_putint(index);
            uart_puts(", chunk size = ");
            uart_putint(pool_sizes[index]);
            uart_puts("\n");
        }
        
    }

    int first_allocate_new_chunk = 0;
    if (!pool->free_list) {
        debug++;

        void* page_add = allocate_new_chunk_page(pool);
        if(page_add == NULL){
            irq_restore(state);
            return NULL;
        }
        first_allocate_new_chunk = 1;
    }

    chunk_t *chunk = pool->free_list;
    if (chunk == NULL) {
        irq_restore(state);
        return NULL;
    }

    pool->free_list = chunk->next;
    chunk->next = NULL;
    uintptr_t page_base = (uintptr_t)chunk & ~(PAGE_SIZE - 1);
    if (!first_allocate_new_chunk) {
        chunk_page_t *pg = pool->pages;
        int found_page = 0;
        while (pg) {
            if((uintptr_t)pg->page_addr == page_base){
                if (pg->free_chunks <= 0) {
                    uart_puts("[kmalloc] Invalid chunk page accounting\n");
                    chunk->next = pool->free_list;
                    pool->free_list = chunk;
                    irq_restore(state);
                    return NULL;
                }
                pg->free_chunks--;
                found_page = 1;
                break;
            }
            pg = pg->next;
        }
        if (!found_page) {
            uart_puts("[kmalloc] Cannot find chunk page metadata\n");
            chunk->next = pool->free_list;
            pool->free_list = chunk;
            irq_restore(state);
            return NULL;
        }
        if(DEMO) print_free_list();
    }
    
    if(DEMO){
        print_pool_state();
        
    }
    irq_restore(state);
    return (void *)chunk;
}

void kfree(void *ptr) {
    irq_state_t state = irq_save();

    if (ptr == NULL) {
        irq_restore(state);
        return;
    }

    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t page_base = addr & ~(PAGE_SIZE - 1);

    if(DEMO){
        uart_puts("[kfree] ptr = ");
        uart_put_hex(addr); NL;
        uart_puts("[kfree] page base = ");
        uart_put_hex(page_base); NL;
    }
    buddy_block* block = lookup_allocated_block(ptr);
    if (block == NULL) {
        uart_puts("[kfree] block not found in node_table\n");
        irq_restore(state);
        return;
    }

    if (block->status == BUDDY_BLOCK_RESERVED) {
        if(DEMO)uart_puts("[kfree] Reserved memory! command cancel\n");
        irq_restore(state);
        return;
    }

    if (block->status != BUDDY_BLOCK_CHUNK_PAGE) {
        if(DEMO)uart_puts("[kfree] → Not a chunk, calling buddy_free\n");
        buddy_free(ptr);
        if(DEMO) print_free_list();
        irq_restore(state);
        return;
    }
    
    if(DEMO)uart_puts("[kfree] → This is a CHUNK\n");

    memory_pool_t *pool = NULL;
    chunk_page_t *pg = NULL;
    chunk_page_t *prev_pg = NULL;
    int found = 0;
    for (int i = 0; i < NUM_POOLS; i++) {
        pool = &mem_pools[i];
        prev_pg = NULL;
        for (chunk_page_t *tmp_pg = pool->pages;
             tmp_pg != NULL; tmp_pg = tmp_pg->next) {
            if((uintptr_t)tmp_pg->page_addr == page_base){
                pg = tmp_pg;
                found = 1;
                break;
            }
            prev_pg = tmp_pg;
        }
        if (found) {
            break;
        }
    }

    if (!found || pool == NULL || pg == NULL) {
        uart_puts("[kfree] cannot find chunk_page for addr\n");
        irq_restore(state);
        return;
    }

    uintptr_t offset = addr - page_base;
    if (offset >= PAGE_SIZE || pool->chunk_size == 0 ||
        (offset % pool->chunk_size) != 0 ||
        offset + pool->chunk_size > PAGE_SIZE ||
        offset / pool->chunk_size >= (uintptr_t)pg->total_chunks) {
        uart_puts("[kfree] address is not a valid chunk boundary\n");
        irq_restore(state);
        return;
    }

    if (pg->free_chunks >= pg->total_chunks || in_chunk_freelist(pool, ptr)) {
        uart_puts("[kfree] The chunk has not been allocated.\n");
        irq_restore(state);
        return;
    }
    if(DEMO){
        uart_puts("[kfree] matched chunk size: ");
        uart_putint(pool->chunk_size); NL;
        uart_putint(found);NL;
    }

    if(DEBUG || DEMO){
        uart_puts("[kfree] chunk page found → page addr: ");
        uart_put_hex((uintptr_t)pg->page_addr); NL;

        uart_puts("[kfree] After free chucnk: ");
        uart_putint(pg->total_chunks);
        uart_puts(", free_chunks: ");
        uart_putint(pg->free_chunks + 1); NL;
    }
    

    chunk_t *chunk = (chunk_t *)ptr;
    chunk->next = pool->free_list;
    pool->free_list = chunk;
    pg->free_chunks++;
    if(DEBUG)
    uart_puts("[kfree] chunk returned to pool\n");

    int chunk_free = 0;
    if (pg->free_chunks == pg->total_chunks) {
        if(DEBUG||DEMO)
        uart_puts("[kfree] All chunks freed, releasing page\n");

        uintptr_t start = (uintptr_t)pg->page_addr;
        uintptr_t end = start + PAGE_SIZE;

        chunk_t* new_list = NULL;
        chunk_t* cur = pool->free_list;
        while (cur) {
            chunk_t* next = cur->next;
            uintptr_t cur_addr = (uintptr_t)cur;
            if (cur_addr < start || cur_addr >= end) {
                cur->next = new_list;
                new_list = cur;
            }
            cur = next;
        }
        pool->free_list = new_list;

        void *released_page = pg->page_addr;
        if (prev_pg != NULL) {
            prev_pg->next = pg->next;
        } else {
            pool->pages = pg->next;
        }
        pg->page_addr = NULL;
        pg->total_chunks = 0;
        pg->free_chunks = 0;
        pg->next = NULL;
        if (pool->num_page > 0) {
            pool->num_page--;
        }
        buddy_free(released_page);
        chunk_free = 1;
    }
    
    if(!chunk_free){ // chunk_free=1 代表有釋放page，所以有輸出過free_list
        if(DEBUG) print_free_list();
    }
    if(DEMO){
        print_pool_state();
        print_free_list();
        
    } 
    irq_restore(state);
    return;
}

int buddy_page_get(void *page) {
    if (!page || ((uintptr_t)page & (PAGE_SIZE - 1)))
        return -1;

    irq_state_t state = irq_save();
    buddy_block *block = lookup_allocated_block(page);
    if (!block || block->addr != page || block->size != PAGE_SIZE ||
        block->status != BUDDY_BLOCK_DIRECT || block->refcount == 0 ||
        block->refcount == 0xFFFFFFFFU) {
        irq_restore(state);
        return -1;
    }
    block->refcount++;
    irq_restore(state);
    return 0;
}

// =================================== startup ===================================
void startup_allocator_init() {
    metadata_cursor = (uintptr_t)BUDDY_METADATA_ADDR;
    reserved_count = 0;

    /* The metadata arena is carved out once, before the buddy allocator is
     * usable.  It is reserved below so it can never be returned by kmalloc. */

    // 1. buddy_system control block
    buddy = (buddy_system *)metadata_cursor;
    metadata_cursor += sizeof(buddy_system);
    align(&metadata_cursor, 16);

    // 2. One free-list head for every block order.
    buddy->free_lists = (buddy_block **)metadata_cursor;
    metadata_cursor += sizeof(buddy_block *) * MAX_ORDER;
    align(&metadata_cursor, 16);

    // 3. One node-table entry for every node in the complete buddy tree.
    buddy->node_table = (buddy_node_entry *)metadata_cursor;
    metadata_cursor += sizeof(buddy_node_entry) * MAX_ALLOC;
    align(&metadata_cursor, 16);
    
    // 4. No block is allocatable until buddy_init() puts the root on a list.
    for (int i = 0; i < MAX_ORDER; ++i) {
        buddy->free_lists[i] = NULL;
    }
    
    node_metadata_cursor = (buddy_block *)metadata_cursor;
    align(&node_metadata_cursor, 16);
    for (int order = MAX_ORDER - 1; order >= 0; order--) {
        for (int local_idx = 0;
             local_idx < block_count_for_order(order);
             ++local_idx) {
            int i = node_index_for_order(order, local_idx);
            
            uintptr_t block_size = block_size_for_order(order);
            // Static identity of this buddy-tree node.
            buddy->node_table[i].block = node_metadata_cursor;
            buddy->node_table[i].block->first_page = local_idx * (1 << order);
            buddy->node_table[i].block->status = BUDDY_BLOCK_DIRECT;
            buddy->node_table[i].block->addr = (void *)
                ((uintptr_t)local_idx * block_size);
            buddy->node_table[i].block->size = block_size;
            buddy->node_table[i].block->prev = NULL;
            buddy->node_table[i].block->next = NULL;
            buddy->node_table[i].block->node_id = i;
            buddy->node_table[i].block->on_free_list = 0;
            buddy->node_table[i].block->refcount = 0;

            buddy->node_table[i].availability = BUDDY_NODE_AVAILABLE;

            node_metadata_cursor += 1;
            align(&node_metadata_cursor, 16);
        }
    }
    metadata_cursor = (uintptr_t)node_metadata_cursor;
    
    // 處理 pool init
    chunk_page_metadata_cursor = (chunk_page_t*)(uintptr_t)metadata_cursor;
    align(&chunk_page_metadata_cursor, 16);
    
    
    metadata_cursor = (uintptr_t)(chunk_page_metadata_cursor + (NUM_POOLS *  MAX_PAGE_NUM_FOR_CHUNK));
    
    align(&metadata_cursor, 16);
    for (int i = 0; i < NUM_POOLS; i++) {
        mem_pools[i].chunk_size = pool_sizes[i];
        mem_pools[i].free_list = NULL;
        mem_pools[i].num_page = 0;
        chunk_page_t *metadata_base = chunk_page_metadata_cursor +
            i * MAX_PAGE_NUM_FOR_CHUNK;
        mem_pools[i].pages = NULL;
        for (int j = 0; j < MAX_PAGE_NUM_FOR_CHUNK; j++) {
            chunk_page_t *page = &metadata_base[j];
            page->page_addr = NULL;
            page->total_chunks = 0;
            page->free_chunks = 0;
            page->next = NULL;
        }
    }

    /* Every byte outside the Lab 4 usable range is unavailable. */
    memory_reserve((void*)BUDDY_END, (void*)BUDDY_ROOT_END);
    memory_reserve((void*)BUDDY_METADATA_ADDR, (void*)metadata_cursor);
    /* Keep the entire first 2 MB out of the allocator.  User images start at
     * VA 0, so their PTEs can replace this low region without hiding any
     * kernel object addressed through the allocator's physical pointers. */
    memory_reserve((void*)0x0000, (void*)0x00200000);
    memory_reserve((void *)KVA_TO_PHYS(_kernel_start),
                   (void *)KVA_TO_PHYS(_end)); // Kernel image
    memory_reserve((void*)BOOT_PGD_PA, (void*)(BOOT_PUD_PA + PAGE_SIZE));

    uintptr_t dtb_start = dtb_addr;
    uintptr_t dtb_end = dtb_start;
    if (dtb_start != 0 && dtb_start >= BUDDY_START &&
        dtb_start <= BUDDY_ROOT_END - sizeof(struct fdt_header)) {
        struct fdt_header *hdr = (struct fdt_header *)dtb_start;
        if (be32_to_cpu(&hdr->magic) == FDT_MAGIC) {
            uint32_t dtb_size = be32_to_cpu(&hdr->totalsize);
            if (dtb_size > 0 && dtb_size <= BUDDY_ROOT_END - dtb_start) {
                dtb_end = dtb_start + dtb_size;
            }
        }
    }
    if (dtb_end > dtb_start) {
        memory_reserve((void*)dtb_start, (void*)dtb_end);
    }

    uintptr_t ramdisk_start = KVA_TO_PHYS(RAMDISK_BASE);
    uintptr_t ramdisk_end = KVA_TO_PHYS(RAMDISK_END);
    if (ramdisk_end > ramdisk_start) {
        memory_reserve((void*)ramdisk_start, (void*)ramdisk_end);
    }
    buddy_init();

}


void memory_reserve(void *start, void* end) {
    uintptr_t range_start = (uintptr_t)start;
    uintptr_t range_end = (uintptr_t)end;

    if (range_start < BUDDY_START) {
        range_start = BUDDY_START;
    }
    if (range_end > BUDDY_ROOT_END) {
        range_end = BUDDY_ROOT_END;
    }
    range_start = align_down(range_start, PAGE_SIZE);
    range_end = align_up(range_end, PAGE_SIZE);
    if (range_end > BUDDY_ROOT_END) {
        range_end = BUDDY_ROOT_END;
    }
    if (range_start >= range_end) {
        return;
    }

    /* Keep ranges sorted and coalesce overlaps/adjacent ranges. */
    int first = 0;
    while (first < reserved_count &&
           reserved_ranges[first].end < range_start) {
        first++;
    }

    int last = first;
    while (last < reserved_count &&
           reserved_ranges[last].start <= range_end) {
        if (reserved_ranges[last].start < range_start) {
            range_start = reserved_ranges[last].start;
        }
        if (reserved_ranges[last].end > range_end) {
            range_end = reserved_ranges[last].end;
        }
        last++;
    }

    if (first == last) {
        if (reserved_count >= MAX_RESERVED) {
            uart_puts("[memory_reserve] Too many reserved ranges\n");
            return;
        }
        for (int i = reserved_count; i > first; --i) {
            reserved_ranges[i] = reserved_ranges[i - 1];
        }
        reserved_count++;
    } else {
        for (int i = last; i < reserved_count; ++i) {
            reserved_ranges[first + 1 + i - last] = reserved_ranges[i];
        }
        reserved_count -= last - first - 1;
    }

    reserved_ranges[first].start = range_start;
    reserved_ranges[first].end = range_end;

    if(DEMO){
        uart_puts("\n\nreserve start address: ");
        uart_put_hex(range_start);NL;
        uart_puts("reserve end address: ");
        uart_put_hex(range_end);NL;
    }
    
}

/* Reserve a half-open address range using the largest aligned buddy blocks. */
static int reserve_range(uintptr_t start, uintptr_t end) {
    if (start >= end) {
        return 0;
    }

    if (start < BUDDY_START) {
        start = BUDDY_START;
    }
    if (end > BUDDY_ROOT_END) {
        end = BUDDY_ROOT_END;
    }
    if (start >= end) {
        return 0;
    }

    start = align_down(start, PAGE_SIZE);
    end = align_up(end, PAGE_SIZE);
    if (end > BUDDY_ROOT_END) {
        end = BUDDY_ROOT_END;
    }

    uintptr_t cursor = start;
    while (cursor < end) {
        int order = MAX_ORDER - 1;
        while (order > 0) {
            uintptr_t block_size = PAGE_SIZE << order;
            if ((cursor & (block_size - 1)) == 0 &&
                block_size <= end - cursor) {
                break;
            }
            order--;
        }

        uintptr_t block_size = PAGE_SIZE << order;
        buddy_block *target = block_for_page_and_order(
            (int)(cursor / PAGE_SIZE), 0);
        if (target != NULL) {
            (void)reserve_startup_allocations((unsigned int)block_size, target);
        }
        buddy_block *reserved_block = block_for_page_and_order(
            (int)(cursor / PAGE_SIZE), order);
        if (target == NULL || reserved_block == NULL ||
            reserved_block->status != BUDDY_BLOCK_RESERVED ||
            get_node_availability(reserved_block) != BUDDY_NODE_IN_USE) {
            uart_puts("[buddy_init] unable to reserve range block\n");
            return -1;
        }
        cursor += block_size;
    }
    return 0;
}

// =================================== tool ===================================

static char* block_state_to_str(int status) {
    switch (status) {
        case BUDDY_BLOCK_RESERVED:   return "reserved";
        case BUDDY_BLOCK_SPLIT:      return "split";
        case BUDDY_BLOCK_DIRECT:     return "direct";
        case BUDDY_BLOCK_CHUNK_PAGE: return "chunk-page";
        default:
            return "unknown";
    }
}

static char* node_availability_to_str(int availability) {
    switch (availability) {
        case BUDDY_NODE_AVAILABLE: return "available";
        case BUDDY_NODE_IN_USE:    return "in-use";
        default:                    return "unknown";
    }
}

static int buddy_first_page_for_order(int first_page, int order) {
    return first_page ^ (1 << order);
}

static int block_count_for_order(int order) {
    return 1 << (MAX_ORDER - order - 1);
}

static uintptr_t block_size_for_order(int order) {
    return PAGE_SIZE << order;
}

/* Complete-binary-tree layout: the nodes for one order are contiguous. */
static int node_index_for_order(int order, int local_block_index) {
    return block_count_for_order(order) - 1 + local_block_index;
}

static void push_free_block(int order, buddy_block *blk) {
    if (buddy == NULL || blk == NULL || order < 0 || order >= MAX_ORDER ||
        blk->on_free_list) {
        return;
    }

    blk->next = buddy->free_lists[order];
    if (buddy->free_lists[order]) {
        buddy->free_lists[order]->prev = blk;
    }
    blk->prev = NULL;
    buddy->free_lists[order] = blk;

    blk->status = BUDDY_BLOCK_DIRECT;
    blk->refcount = 0;
    set_node_availability(blk, BUDDY_NODE_AVAILABLE);
    blk->on_free_list = 1;
}

static void remove_free_block(int order, buddy_block *blk) {
    if (buddy == NULL || blk == NULL || order < 0 || order >= MAX_ORDER ||
        !blk->on_free_list) {
        return;
    }

    if (blk->prev != NULL) {
        blk->prev->next = blk->next;
    } else if (buddy->free_lists[order] == blk) {
        buddy->free_lists[order] = blk->next;
    } else {
        return;
    }

    if (blk->next != NULL) {
        blk->next->prev = blk->prev;
    }
    blk->prev = NULL;
    blk->next = NULL;
    blk->on_free_list = 0;
}

static buddy_block *pop_free_block(int order) {
    if (buddy == NULL || order < 0 || order >= MAX_ORDER) {
        return NULL;
    }

    buddy_block *blk = buddy->free_lists[order];
    if (blk == NULL) {
        return NULL;
    }
    remove_free_block(order, blk);
    return blk;
}

/* Map an arbitrary pointer back to the in-use buddy node that contains it. */
static buddy_block* lookup_allocated_block(void* addr) {
    if (buddy == NULL || addr == NULL) {
        return NULL;
    }

    uintptr_t address = (uintptr_t)addr;
    if (address < BUDDY_START || address >= BUDDY_ROOT_END) {
        return NULL;
    }

    for (int order = 0; order < MAX_ORDER; order++) {
        uintptr_t block_size = block_size_for_order(order);
        int local_block_index = (int)((address - BUDDY_START) / block_size);
        int block_count = block_count_for_order(order);
        if (local_block_index < 0 || local_block_index >= block_count) {
            continue;
        }

        int node_index = node_index_for_order(order, local_block_index);
        buddy_node_entry *entry = &buddy->node_table[node_index];
        if (entry->availability == BUDDY_NODE_IN_USE && entry->block != NULL &&
            entry->block->status != BUDDY_BLOCK_SPLIT &&
            address >= (uintptr_t)entry->block->addr &&
            address < (uintptr_t)entry->block->addr + entry->block->size) {
            return entry->block;
        }
    }
    return NULL;
}

void print_alloc(){
    if(DEBUG_ALL){
        uart_puts("[alloc]\n");
        for (int i = 0; i < MAX_ALLOC; i++) {
            buddy_node_entry *entry = &buddy->node_table[i];
            if (entry->availability == BUDDY_NODE_IN_USE) {
                uart_put_hex((uintptr_t)entry->block->addr);
                uart_puts("\t");
                uart_puts(node_availability_to_str(entry->availability));
                uart_puts("\t");
                uart_putint(entry->block->size);
                uart_puts("\t");
                uart_putint(entry->block->first_page);
                uart_puts("\t");
                uart_putint((uintptr_t)entry->block->addr);
                uart_puts("\t");
                uart_putint(entry->block->node_id);
                uart_puts("\t");
                uart_puts(block_state_to_str(entry->block->status));
                
                NL;
                
            }
        }
        uart_puts("[end alloc]\n");
    }

}

void print_free_list() {
    uart_puts("[free_list]\n");
    for (int order = 0; order < MAX_ORDER; order++) {
        uart_puts("Order ");
        uart_putint(order);
        uart_puts(":\t");
        buddy_block *cur = buddy->free_lists[order];
        while (cur) {
            uart_puts(" [addr = ");
            uart_put_hex((uintptr_t)cur->addr);
            uart_puts("]");
            cur = cur->next;
        }
        NL;
    }
    uart_puts("[end]\n\n");
}

int find_pool_index(size_t size) {
    for (int i = 0; i < NUM_POOLS; i++) {
        if (size <= pool_sizes[i]) {
            return i;
        }
    }
    
    return -1;  // 超過 max pool size，fallback 用 buddy
}

void* allocate_new_chunk_page(memory_pool_t *pool) {
    if (pool == NULL || pool->num_page >= MAX_PAGE_NUM_FOR_CHUNK ||
        pool->chunk_size == 0 || PAGE_SIZE / pool->chunk_size == 0) {
        return NULL;
    }

    int pool_index = (int)(pool - mem_pools);
    if (pool_index < 0 || pool_index >= NUM_POOLS ||
        chunk_page_metadata_cursor == NULL) {
        return NULL;
    }

    chunk_page_t *metadata_base = chunk_page_metadata_cursor +
        pool_index * MAX_PAGE_NUM_FOR_CHUNK;
    chunk_page_t *new_pg = NULL;
    for (int i = 0; i < MAX_PAGE_NUM_FOR_CHUNK; i++) {
        if (metadata_base[i].page_addr == NULL) {
            new_pg = &metadata_base[i];
            break;
        }
    }
    if (new_pg == NULL) {
        uart_puts("[allocator] chunk metadata is full\n");
        return NULL;
    }

    void *page = buddy_malloc(PAGE_SIZE, 1);
    if(DEMO){
        uart_puts("page: ");
        uart_put_hex((uintptr_t)page);
        NL;
    }
    if (!page) return NULL;

    int chunk_count = PAGE_SIZE / pool->chunk_size;
    for (int i = 0; i < chunk_count; i++) {
        chunk_t *chunk = (chunk_t *)((char *)page + i * pool->chunk_size);
        chunk->next = pool->free_list;
        pool->free_list = chunk;
    }
    

    new_pg->page_addr = page;
    new_pg->total_chunks = chunk_count;
    new_pg->free_chunks = chunk_count - 1;
    new_pg->next = pool->pages;
    pool->pages = new_pg;
    pool->num_page++;
    // chunk_page_metadata_cursor += 1;
    return page;
}

void print_pool_state() {
    uart_puts("[pool state]\n");
    for (int i = 0; i < NUM_POOLS; i++) {
        uart_puts("Pool ");
        uart_putint(i);
        uart_puts(" (size ");
        uart_putint(pool_sizes[i]);
        uart_puts("): ");

        int count = 0;
        chunk_t* cur = mem_pools[i].free_list;
        while (cur) {
            count++;
            cur = cur->next;
        }
        uart_putint(count);
        uart_puts(" chunks\t");
        uart_puts("page_num: ");
        uart_putint(mem_pools[i].num_page);
        NL;
    }
    uart_puts("[pool state end]\n");
    NL;
}

static buddy_block* block_for_page_and_order(int page_index, int order) {
    if (buddy == NULL || page_index < 0 ||
        page_index >= (int)BUDDY_PAGE_COUNT || order < 0 ||
        order >= MAX_ORDER) {
        return NULL;
    }

    int pages_per_block = 1 << order;
    int local_block_index = page_index / pages_per_block;
    if (local_block_index < 0 ||
        local_block_index >= block_count_for_order(order)) {
        return NULL;
    }
    return buddy->node_table[node_index_for_order(order, local_block_index)].block;
    
}

static buddy_block* parent_block(buddy_block* blk){
    if (blk == NULL || blk->node_id <= 0) { // root
        return NULL;
    }
    return buddy->node_table[(blk->node_id-1)/2].block;
}

static void set_node_availability(buddy_block* blk, int availability){
    if (buddy != NULL && blk != NULL && blk->node_id >= 0 && blk->node_id < MAX_ALLOC) {
        buddy->node_table[blk->node_id].availability = availability;
    }
}

static int get_node_availability(buddy_block* blk){
    if (buddy == NULL || blk == NULL || blk->node_id < 0 || blk->node_id >= MAX_ALLOC) {
        return BUDDY_NODE_IN_USE;
    }
    return buddy->node_table[blk->node_id].availability;
}

int in_chunk_freelist(memory_pool_t* m_pool, void* add){
    if (m_pool == NULL || add == NULL) {
        return 0;
    }
    for(chunk_t* p = m_pool->free_list; p!= NULL; p=p->next){
        if(p == add){
            return 1;
        }
    }
    return 0;
}

int be_used(void* page_addr){
    if (page_addr == NULL) {
        return 0;
    }
    uintptr_t address = (uintptr_t)page_addr;
    if (address < BUDDY_START || address >= BUDDY_ROOT_END) {
        return 0;
    }
    uint32_t first_page = (uint32_t)((address - BUDDY_START) / PAGE_SIZE);
    buddy_block* blk = block_for_page_and_order(first_page, 0);
    for(uint32_t i = 0; i < MAX_ORDER; ++i){
        if (blk == NULL) {
            return 0;
        }
        if(blk->on_free_list)return 1;
        blk = parent_block(blk);
    }

    return 0;

    // for(uint32_t i=0;i<MAX_ORDER;++i)
    // for(buddy_block* b = buddy->free_lists[i]; b!= NULL; b=b->next){
    //     if(b->addr == page_addr){
    //         return 1;
    //     }
    // }
    // return 0;
}
