#include <allocator.h>
#include <utils.h>
#include <uart.h>
#include <exception_.h>
#include <device_tree.h>
#include <ramdisk.h>

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
/* ==================== for buddy system global variables ==================== */
buddy_system *buddy = 0;
void *buddy_mem_start = (void*)BUDDY_START;
int buddy_block_cursor = 0;
buddy_block* block_metadata_cursor = 0;
chunk_page_t* chunk_metadata_cursor = 0;
/* ========================================================== */

/* ==================== for memory pool ==================== */
memory_pool_t mem_pools[NUM_POOLS];
/* ========================================================== */

/* ==================== for startup memory setting ==================== */
static uintptr_t current = BUDDY_METADATA_ADDR;
reserved_range_t reserved_ranges[MAX_RESERVED];
int reserved_count = 0;
/* ========================================================== */
/* ==================== some tools for buddy system ==================== */
char* block_state_to_str(int status);
char* block_valid_to_str(int valid);
int find_buddy_index(int index, int order);
buddy_block* get_parent_block(buddy_block* blk);
void push_to_freelist(int order, buddy_block *blk);
buddy_block* pop_from_freelist(int order);
buddy_block* choose_from_freelist(int order, buddy_block* target_blk);
void remove_from_freelist(int order, buddy_block *blk);
void set_entry_valid(buddy_block* blk, int val);
int get_entry_valid(buddy_block* blk);


buddy_block* lookup_alloc(void* addr);
// void print_free_list();
// void print_alloc();
buddy_block* get_block_by_page_idx_and_order(int page_idx, int order);
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
    chunk_metadata_cursor = (chunk_page_t*)(uintptr_t)block_metadata_cursor;
    
    align(&chunk_metadata_cursor, 16);

    // pool init
    for (int i = 0; i < NUM_POOLS; i++) {
        mem_pools[i].chunk_size = pool_sizes[i];
        mem_pools[i].free_list = NULL;
    }
    

    // 建立最上層 block
    buddy_block_cursor = 1;
    buddy_block *root = buddy->alloc_table[0].block;
    root->idx = 0;
    root->status = BLOCK;
    root->addr = (void *)BUDDY_START;
    root->size = BUDDY_ROOT_SIZE;
    root->prev = NULL;
    root->next = NULL;
    root->in_free_list = 0;
    set_entry_valid(root, UNUSED);
    push_to_freelist(MAX_ORDER - 1, root);
    

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
    
    
    // uart_put_hex((void*)buddy->free_list[order]);
    // uart_puts("hi");
    while (order < MAX_ORDER && buddy->free_list[order] == NULL){
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
    
    
    buddy_block *blk = pop_from_freelist(order);
    if (blk == NULL) {
        return NULL;
    }

    set_entry_valid(blk, USED);

    while (order > target_order) {
        order--;

        buddy_block *left = get_block_by_page_idx_and_order(blk->idx, order);
        buddy_block *right = get_block_by_page_idx_and_order(
            blk->idx + (1 << order), order);
        if (left == NULL || right == NULL) {
            return NULL;
        }

        /* The old block is now an internal node; allocate its left child. */
        blk->status = BLOCK_SPLIT;
        set_entry_valid(blk, USED);
        blk->prev = NULL;
        blk->next = NULL;
        blk->in_free_list = 0;

        left->status = BLOCK;
        left->prev = NULL;
        left->next = NULL;
        left->in_free_list = 0;
        set_entry_valid(left, USED);

        right->status = BLOCK;
        right->prev = NULL;
        right->next = NULL;
        right->in_free_list = 0;
        set_entry_valid(right, UNUSED);
        push_to_freelist(order, right);
        blk = left;
        
        if(DEBUG){
            uart_puts("[malloc] Split block →\n");
            uart_puts("  Left : addr = ");
            uart_put_hex((uintptr_t)blk->addr);
            uart_puts(", idx = ");
            uart_putint(blk->idx);
            uart_puts(", size = ");
            uart_putint(blk->size);
            uart_puts(", order = ");
            uart_putint(order);
            uart_puts(", buddy block status = ");
            uart_puts(block_state_to_str(right->status));
            uart_puts("\n");
            uart_puts("  Right: addr = ");
            uart_put_hex((uintptr_t)right->addr);
            uart_puts(", idx = ");
            uart_putint(right->idx);
            uart_puts(", size = ");
            uart_putint(right->size);
            uart_puts(", order = ");
            uart_putint(order);
            NL;
        }

    }
    if(chunk){
        blk->status = BLOCK_CHUNK;
        
    } 
    else {
        blk->status = BLOCK;
        set_entry_valid(blk, USED);
        // buddy->alloc_table[blk->idd].valid = USED;
    }

    
    if(DEBUG){
        uart_puts("[malloc] Allocated block: addr = ");
        uart_put_hex((uintptr_t)blk->addr);
        uart_puts(", size = ");
        uart_putint(blk->size);
        uart_puts(", idx = ");
        uart_putint(blk->idx);
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

    int target_page_idx = target_blk->idx;
    if (target_page_idx < 0 || target_page_idx >= (int)BUDDY_PAGE_COUNT ||
        (target_page_idx & ((1 << target_order) - 1)) != 0 ||
        target_blk->size != PAGE_SIZE) {
        uart_puts("Error: Invalid startup allocation target\n");
        return NULL;
    }

    if (get_entry_valid(target_blk) == USED &&
        target_blk->status != BLOCK_SPLIT) {
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
        buddy_block *candidate = get_block_by_page_idx_and_order(
            target_page_idx, candidate_order);
        if (candidate != NULL && candidate->status == BLOCK &&
            get_entry_valid(candidate) == UNUSED &&
            candidate->in_free_list) {
            order = candidate_order;
            blk = candidate;
            break;
        }
    }

    if (blk == NULL || order < target_order) {
        uart_puts("No available block for startup reservation\n");
        return NULL;
    }
    remove_from_freelist(order, blk);
    if (blk->in_free_list) {
        uart_puts("No available block for startup reservation\n");
        return NULL;
    }

    if(DEBUG){
        uart_puts("[reserve] Using block from order = ");
        uart_putint(order);
        uart_puts("\n");
    }

    set_entry_valid(blk, USED);
    blk->status = (order == target_order) ? BLOCK_RESERVED : BLOCK_SPLIT;
    blk->prev = NULL;
    blk->next = NULL;
    blk->in_free_list = 0;

    while (order > target_order) {
        order--;

        buddy_block *left = get_block_by_page_idx_and_order(blk->idx, order);
        buddy_block *right = get_block_by_page_idx_and_order(
            blk->idx + (1 << order), order);
        if (left == NULL || right == NULL) {
            uart_puts("Error: Could not split startup reservation\n");
            return NULL;
        }

        int target_in_right = target_page_idx >= right->idx;
        buddy_block *chosen = target_in_right ? right : left;
        buddy_block *sibling = target_in_right ? left : right;

        blk->status = BLOCK_SPLIT;
        set_entry_valid(blk, USED);
        blk->prev = NULL;
        blk->next = NULL;
        blk->in_free_list = 0;

        sibling->status = BLOCK;
        sibling->prev = NULL;
        sibling->next = NULL;
        sibling->in_free_list = 0;
        set_entry_valid(sibling, UNUSED);
        push_to_freelist(order, sibling);

        chosen->prev = NULL;
        chosen->next = NULL;
        chosen->in_free_list = 0;
        set_entry_valid(chosen, USED);
        chosen->status = (order == target_order) ? BLOCK_RESERVED : BLOCK_SPLIT;
        blk = chosen;
        
        if(DEBUG){
            uart_puts("[malloc] Split block →\n");
            uart_puts("  main : addr = ");
            uart_put_hex((uintptr_t)blk->addr);
            uart_puts(", idx = ");
            uart_putint(blk->idx);
            uart_puts(", size = ");
            uart_putint(blk->size);
            uart_puts(", order = ");
            uart_putint(order);
            uart_puts(", buddy block status = ");
            uart_puts(block_state_to_str(sibling->status));
            uart_puts("\n");
            uart_puts("  buddy: addr = ");
            uart_put_hex((uintptr_t)sibling->addr);
            uart_puts(", idx = ");
            uart_putint(sibling->idx);
            uart_puts(", size = ");
            uart_putint(sibling->size);
            uart_puts(", order = ");
            uart_putint(order);
            uart_puts("\n");
        }

    }
    
    blk->status = BLOCK_RESERVED;
    set_entry_valid(blk, USED);
    return blk->addr;
}

void* test(int idx){
    buddy_block* target_blk = get_block_by_page_idx_and_order(idx, 0);
    void* add = reserve_startup_allocations(PAGE_SIZE, target_blk);
    return add;
}

void buddy_free(void *addr) {
    if (addr == NULL) {
        return;
    }

    buddy_block *block = lookup_alloc(addr);
    if (block == NULL) {
        uart_puts("[free] ERROR: Unknown addr, cannot free\n");
        return;
    }

    if (block->addr != addr) {
        uart_puts("[free] ERROR: Address is not an allocation base\n");
        return;
    }

    if (block->status == BLOCK_RESERVED) {
        uart_puts("[free] ERROR: Cannot free reserved memory\n");
        return;
    }
    if (block->status != BLOCK && block->status != BLOCK_CHUNK) {
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

    set_entry_valid(block, UNUSED);
    block->status = BLOCK;
    block->prev = NULL;
    block->next = NULL;
    block->in_free_list = 0;

    if(DEBUG){
        uart_puts("[free] Start: addr = ");
        uart_put_hex((uintptr_t)addr);
        uart_puts(", idx = ");
        uart_putint(block->idx);
        uart_puts(", order = ");
        uart_putint(order);
        uart_puts("\n");
    }

    while (order < MAX_ORDER - 1) {
        int buddy_idx = find_buddy_index(block->idx, order);
        buddy_block *buddy_blk = get_block_by_page_idx_and_order(buddy_idx, order);

        if (buddy_blk == NULL || buddy_blk->status != BLOCK ||
            get_entry_valid(buddy_blk) != UNUSED ||
            !buddy_blk->in_free_list) {
            if(DEBUG){
                uart_puts("[free] Cannot merge with buddy idx = ");
                uart_putint(buddy_idx);
                uart_puts(" at order ");
                uart_putint(order);
                uart_puts(", buddy_blk status: ");
                uart_puts(buddy_blk ? block_state_to_str(buddy_blk->status) : "NULL");
                uart_puts(", buddy_blk valid: ");
                uart_puts(buddy_blk ? block_valid_to_str(get_entry_valid(buddy_blk)) : "NULL");
                uart_puts("\n");
            }
            break;
        }
        if(DEMO){
            uart_puts("[free] Merging with buddy idx = ");
            uart_putint(buddy_idx);
            uart_puts(" at order ");
            uart_putint(order);
            uart_puts(", buddy_blk status: ");
            uart_puts(block_state_to_str(buddy_blk->status));
            uart_puts(", buddy_blk addr: ");
            uart_put_hex((uintptr_t)buddy_blk->addr);
            NL;
        }
        
        buddy_block* parent_blk = get_parent_block(block);
        if (parent_blk == NULL) {
            break;
        }

        remove_from_freelist(order, buddy_blk);

        parent_blk->status = BLOCK;
        set_entry_valid(parent_blk, UNUSED);
        parent_blk->prev = NULL;
        parent_blk->next = NULL;
        parent_blk->in_free_list = 0;
        block = parent_blk;
        order++;
    }

    push_to_freelist(order, block);

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
    buddy_block* block = lookup_alloc(ptr);
    if (block == NULL) {
        uart_puts("[kfree] block not found in alloc_table\n");
        irq_restore(state);
        return;
    }

    if (block->status == BLOCK_RESERVED) {
        if(DEMO)uart_puts("[kfree] Reserved memory! command cancel\n");
        irq_restore(state);
        return;
    }

    if (block->status != BLOCK_CHUNK) {
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

// =================================== startup ===================================
void startup_allocator_init() {
    current = (uintptr_t)BUDDY_METADATA_ADDR;
    reserved_count = 0;
    buddy_block_cursor = 0;

    // 1. 配 buddy_system 本體
    buddy = (buddy_system *)current;
    current += sizeof(buddy_system);
    align(&current, 16);

    // 2. 配 free_list[MAX_ORDER]
    buddy->free_list = (buddy_block **)current;
    current += sizeof(buddy_block *) * MAX_ORDER;
    align(&current, 16);

    // 3. 配 alloc_table[MAX_ALLOC]
    buddy->alloc_table = (allocation_entry *)current;
    current += sizeof(allocation_entry) * MAX_ALLOC;
    align(&current, 16);
    
    // 4. 初始化欄位為 NULL/UNUSED
    for (int i = 0; i < MAX_ORDER; ++i) {
        buddy->free_list[i] = NULL;
    }
    
    block_metadata_cursor = (buddy_block *)current;
    align(&block_metadata_cursor, 16);
    int idd = 0;
    for (int order = MAX_ORDER - 1; order >= 0; order--) {
        for (int local_idx = 0; local_idx < (1 << (MAX_ORDER - order - 1)); ++local_idx) {
            int i = (1 << (MAX_ORDER - order - 1)) - 1 + local_idx;
            
            uintptr_t block_size = PAGE_SIZE << order;
            // 1. addr
            buddy->alloc_table[i].addr = (void *)
                ((uintptr_t)local_idx * block_size);

            // 2. block
            buddy->alloc_table[i].block = block_metadata_cursor;
            buddy->alloc_table[i].block->idx = local_idx * (1 << order);
            buddy->alloc_table[i].block->status = BLOCK;
            buddy->alloc_table[i].block->addr = (void *)
                ((uintptr_t)local_idx * block_size);
            buddy->alloc_table[i].block->size = block_size;
            buddy->alloc_table[i].block->prev = NULL;
            buddy->alloc_table[i].block->next = NULL;
            buddy->alloc_table[i].block->idd = idd++;
            buddy->alloc_table[i].block->in_free_list = 0;

            // 3. valid
            buddy->alloc_table[i].valid = UNUSED;

            block_metadata_cursor += 1;
            align(&block_metadata_cursor, 16);
        }
    }
    current = (uintptr_t)block_metadata_cursor;
    
    // 處理 pool init
    chunk_metadata_cursor = (chunk_page_t*)(uintptr_t)current;
    align(&chunk_metadata_cursor, 16);
    
    
    current = (uintptr_t)(chunk_metadata_cursor + (NUM_POOLS *  MAX_PAGE_NUM_FOR_CHUNK));
    
    align(&current, 16);
    for (int i = 0; i < NUM_POOLS; i++) {
        mem_pools[i].chunk_size = pool_sizes[i];
        mem_pools[i].free_list = NULL;
        mem_pools[i].num_page = 0;
        chunk_page_t *metadata_base = chunk_metadata_cursor +
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
    memory_reserve((void*)BUDDY_METADATA_ADDR, (void*)current);
    memory_reserve((void*)0x0000, (void*)0x1000);   // Spin tables
    memory_reserve((void*)_start,(void*) _end);     // Kernel image

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

    uintptr_t ramdisk_start = (uintptr_t)RAMDISK_BASE;
    uintptr_t ramdisk_end = (uintptr_t)RAMDISK_END;
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
        buddy_block *target = get_block_by_page_idx_and_order(
            (int)(cursor / PAGE_SIZE), 0);
        if (target != NULL) {
            (void)reserve_startup_allocations((unsigned int)block_size, target);
        }
        buddy_block *reserved_block = get_block_by_page_idx_and_order(
            (int)(cursor / PAGE_SIZE), order);
        if (target == NULL || reserved_block == NULL ||
            reserved_block->status != BLOCK_RESERVED ||
            get_entry_valid(reserved_block) != USED) {
            uart_puts("[buddy_init] unable to reserve range block\n");
            return -1;
        }
        cursor += block_size;
    }
    return 0;
}

// =================================== tool ===================================

char* block_state_to_str(int status) {
    switch (status) {
        case BLOCK_ALLOCATED: return "ALLOCATED";
        case BLOCK_RESERVED:    return "BLOCK_RESERVED";
        case BLOCK_SPLIT:     return "SPLIT";
        case BLOCK:           return "BLOCK";
        case BLOCK_CHUNK:     return "CHUNK";
        default:
            if (status >= 0)
                return "AVAILABLE";
            else
                return "UNKNOWN";
    }
}

char* block_valid_to_str(int valid) {
    switch (valid) {
        case UNUSED: return "UNUSED";
        case USED:   return "USED";
        
        default:
            return "UNKNOWN";
    }
}

int find_buddy_index(int index, int order) {
    return index ^ (1 << order);
}

void push_to_freelist(int order, buddy_block *blk) {
    if (buddy == NULL || blk == NULL || order < 0 || order >= MAX_ORDER ||
        blk->in_free_list) {
        return;
    }

    blk->next = buddy->free_list[order];
    if (buddy->free_list[order]) {
        buddy->free_list[order]->prev = blk;
    }
    blk->prev = NULL;
    buddy->free_list[order] = blk;

    blk->status = BLOCK;
    set_entry_valid(blk, UNUSED);
    blk->in_free_list = 1;
}

void remove_from_freelist(int order, buddy_block *blk) {
    if (buddy == NULL || blk == NULL || order < 0 || order >= MAX_ORDER ||
        !blk->in_free_list) {
        return;
    }

    if (blk->prev != NULL) {
        blk->prev->next = blk->next;
    } else if (buddy->free_list[order] == blk) {
        buddy->free_list[order] = blk->next;
    } else {
        return;
    }

    if (blk->next != NULL) {
        blk->next->prev = blk->prev;
    }
    blk->prev = NULL;
    blk->next = NULL;
    blk->in_free_list = 0;
}

buddy_block *pop_from_freelist(int order) {
    if (buddy == NULL || order < 0 || order >= MAX_ORDER) {
        return NULL;
    }

    buddy_block *blk = buddy->free_list[order];
    if (blk == NULL) {
        return NULL;
    }
    remove_from_freelist(order, blk);
    return blk;
}

buddy_block *choose_from_freelist(int order, buddy_block* target_blk) {
    if (buddy == NULL || target_blk == NULL || order < 0 || order >= MAX_ORDER) {
        return NULL;
    }

    for(buddy_block* b = buddy->free_list[order]; b!=NULL; b = b->next){
        if(b->idd == target_blk->idd){
            remove_from_freelist(order, b);
            return b;
        }
    }

    return NULL;
}

buddy_block* lookup_alloc(void* addr) {
    if (buddy == NULL || addr == NULL) {
        return NULL;
    }

    uintptr_t address = (uintptr_t)addr;
    if (address < BUDDY_START || address >= BUDDY_ROOT_END) {
        return NULL;
    }

    for (int order = 0; order < MAX_ORDER; order++) {
        uintptr_t block_size = PAGE_SIZE << order;
        int local_block_index = (int)((address - BUDDY_START) / block_size);
        int block_count = 1 << (MAX_ORDER - order - 1);
        if (local_block_index < 0 || local_block_index >= block_count) {
            continue;
        }

        int global_block_index = block_count - 1 + local_block_index;
        allocation_entry *entry = &buddy->alloc_table[global_block_index];
        if (entry->valid == USED && entry->block != NULL &&
            entry->block->status != BLOCK_SPLIT &&
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
            if ( 1||buddy->alloc_table[i].valid == USED) {
                uart_put_hex((uintptr_t)buddy->alloc_table[i].addr);
                uart_puts("\t");
                uart_puts(block_valid_to_str(buddy->alloc_table[i].valid));
                uart_puts("\t");
                uart_putint(buddy->alloc_table[i].block->size);
                uart_puts("\t");
                uart_putint(buddy->alloc_table[i].block->idx);
                uart_puts("\t");
                uart_putint((uintptr_t)buddy->alloc_table[i].block->addr);
                uart_puts("\t");
                uart_putint(buddy->alloc_table[i].block->idd);
                uart_puts("\t");
                uart_puts(block_state_to_str(buddy->alloc_table[i].block->status));
                
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
        buddy_block *cur = buddy->free_list[order];
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
        chunk_metadata_cursor == NULL) {
        return NULL;
    }

    chunk_page_t *metadata_base = chunk_metadata_cursor +
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
    // chunk_metadata_cursor += 1;
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

buddy_block* get_block_by_page_idx_and_order(int page_idx, int order) {
    if (buddy == NULL || page_idx < 0 ||
        page_idx >= (int)BUDDY_PAGE_COUNT || order < 0 ||
        order >= MAX_ORDER) {
        return NULL;
    }

    int pages_per_block = 1 << order;
    int local_block_idx = page_idx / pages_per_block;
    int level = MAX_ORDER - order - 1;
    int block_count = 1 << level;
    if (local_block_idx < 0 || local_block_idx >= block_count) {
        return NULL;
    }
    return buddy->alloc_table[block_count - 1 + local_block_idx].block;
    
}

buddy_block* get_parent_block(buddy_block* blk){
    if (blk == NULL || blk->idd <= 0) { // root
        return NULL;
    }
    return buddy->alloc_table[(blk->idd-1)/2].block;
}

void set_entry_valid(buddy_block* blk, int val){
    if (buddy != NULL && blk != NULL && blk->idd >= 0 && blk->idd < MAX_ALLOC) {
        buddy->alloc_table[blk->idd].valid = val;
    }
}

int get_entry_valid(buddy_block* blk){
    if (buddy == NULL || blk == NULL || blk->idd < 0 || blk->idd >= MAX_ALLOC) {
        return USED;
    }
    return buddy->alloc_table[blk->idd].valid;
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
    uint32_t idx = (uint32_t)((address - BUDDY_START) / PAGE_SIZE);
    buddy_block* blk = get_block_by_page_idx_and_order(idx, 0);
    for(uint32_t i = 0; i < MAX_ORDER; ++i){
        if (blk == NULL) {
            return 0;
        }
        if(blk->in_free_list)return 1;
        blk = get_parent_block(blk);
    }

    return 0;

    // for(uint32_t i=0;i<MAX_ORDER;++i)
    // for(buddy_block* b = buddy->free_list[i]; b!= NULL; b=b->next){
    //     if(b->addr == page_addr){
    //         return 1;
    //     }
    // }
    // return 0;
}
