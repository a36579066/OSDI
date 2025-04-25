#include <allocator.h>
#include <utils.h>
#include <uart.h>
#include <exception_.h>
#include <device_tree.h>
#include <ramdisk.h>

int DEBUG = 0;
int DEMO =  0;
#define DEBUG_ALL 0


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
void set_entry_valid(buddy_block* blk, int val);
int get_entry_valid(buddy_block* blk);


buddy_block* lookup_alloc(void* addr);
// void print_free_list();
// void print_alloc();
buddy_block* get_block_by_page_idx_and_order(int page_idx, int order);
int in_chunk_freelist(memory_pool_t* m_pool, void* add);
int be_used(void* page_addr);
/* ===================================================================== */

/* ==================== some tools for memory pool ==================== */
void print_pool_state();
int find_pool_index(size_t size);
/* ===================================================================== */

void* simple_malloc(size_t size) {
    disable_interrupts();
    //  8-byte alignment
    align(&heap_ptr, 8); 
    char* start_addr = heap_ptr;
    
    if (heap_ptr + size > &_heap_end) {
        return NULL;  // not enough space
    }

    heap_ptr += size;
    // uart_put_hex((uint32_t)(uintptr_t)heap_ptr);
    enable_interrupts();
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
    allocation_entry *entry = &(buddy->alloc_table[buddy_block_cursor]);
    buddy_block_cursor += 1;
    
    entry->block->idx = 0;
    entry->block->status = BLOCK;
    entry->block->addr = (void *)BUDDY_START;
    entry->block->size = (PAGE_SIZE << (MAX_ORDER - 1));
    entry->block->prev = NULL;
    entry->block->next = NULL;
    entry->block->in_free_list = 1;
    buddy->free_list[MAX_ORDER - 1] = entry->block;
    

    uart_puts("Buddy init complete with free list!\n\n");
    
    // 處理先前 reserved 的 region
    for(int i = 0; i < reserved_count; ++i){
        reserved_range_t range = reserved_ranges[i];
        uintptr_t start = align_down(range.start, PAGE_SIZE);
        uintptr_t end   = align_down(range.end, PAGE_SIZE);
        int start_idx = start/PAGE_SIZE;
        int end_idx = end/PAGE_SIZE;
        for(int j = start_idx; j <= end_idx; ++j){
            buddy_block* target_blk = get_block_by_page_idx_and_order(j, 0);
            void* add = reserve_startup_allocations(PAGE_SIZE, target_blk);
            if(DEBUG){
                uart_puts("reserved return: ");
                uart_put_hex((uintptr_t)add);NL;
            }
        }

    }

    if(DEMO){
        uart_puts("init free_list: \n");
        print_free_list();
        print_alloc();
    }
    
}

void *buddy_malloc(unsigned int size, int chunk) {
    if (size == 0 || size > (PAGE_SIZE << (MAX_ORDER - 1))) {
        if(DEMO)uart_puts("Error: Size out of range\n");
        return NULL;
    }

    int target_order = 0;
    unsigned int total_size = PAGE_SIZE;
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
    blk->status = BLOCK;
    set_entry_valid(blk, USED);
    
    while (order > target_order) {
        order--;
        
        int buddy_blk_idx = find_buddy_index(blk->idx, order);
       
        buddy_block* buddy_blk = get_block_by_page_idx_and_order(buddy_blk_idx, order);
        
        int blk_idx = find_buddy_index(buddy_blk->idx, order);
        blk = get_block_by_page_idx_and_order(blk_idx, order);
        
        buddy_blk->status = BLOCK;
        buddy_blk->prev = buddy_blk->next = NULL;

        if(get_entry_valid(buddy_blk) != USED) push_to_freelist(order, buddy_blk);
        
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
            uart_puts(block_state_to_str(buddy_blk->status));
            uart_puts("\n");
            uart_puts("  Right: addr = ");
            uart_put_hex((uintptr_t)buddy_blk->addr);
            uart_puts(", idx = ");
            uart_putint(buddy_blk->idx);
            uart_puts(", size = ");
            uart_putint(buddy_blk->size);
            uart_puts(", order = ");
            uart_putint(order);
            NL;
        }

        blk->status = BLOCK;
        set_entry_valid(blk, USED);
        
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
    if (size == 0 || size > (PAGE_SIZE << (MAX_ORDER - 1))) {
        uart_puts("Error: Size out of range\n");
        return NULL;
    }
    if(get_entry_valid(target_blk) == USED){
        uart_puts("Error: addr already be used\n");
        return NULL;
    }

    int target_order = 0;
    if(DEBUG){
        uart_puts("[malloc] Requested size = ");
        uart_putint(size);
        uart_puts(", target order = ");
        uart_putint(target_order);
        uart_puts("\n");
    }
    buddy_block* start_block;
    int order = 0;
    {
        buddy_block* blk = target_blk;
        start_block = blk;
        while (order < MAX_ORDER ){//&& buddy->free_list[order] == NULL
            buddy_block* buddy_blk = get_block_by_page_idx_and_order(find_buddy_index(blk->idx, order), order);
            if(order != MAX_ORDER -1 && buddy_blk->status != BLOCK_RESERVED){
                blk = get_parent_block(blk);
                start_block = blk;
                order++;
            }else{
                
                break;
            }
        }
    }
        

    if (order < 0) {
        uart_puts("No available block\n");
        return NULL;
    }
    if(DEBUG){
        uart_puts("[malloc] Using block from order = ");
        uart_putint(order);
        uart_puts("\n");
    }
    
    
    buddy_block *blk = choose_from_freelist(order, start_block);
    blk->status = BLOCK_RESERVED;
    set_entry_valid(blk, USED);
    // buddy->alloc_table[blk->idd].valid = USED;
    
    int base = blk->idx;
    while (order > target_order) {
        order--;
        // int half_size = blk->size / 2; //order = 1
        int buddy_blk_idx;
        buddy_block* buddy_blk;
        if(target_blk->idx - base >= (1<<order)){
            buddy_blk = get_block_by_page_idx_and_order(blk->idx, order);
            buddy_blk_idx = find_buddy_index(blk->idx, order);
            blk = get_block_by_page_idx_and_order(buddy_blk_idx, order);
            base += (1 << order);
        }
        else{
            buddy_blk_idx = find_buddy_index(blk->idx, order);
            buddy_blk = get_block_by_page_idx_and_order(buddy_blk_idx, order);
            blk = get_block_by_page_idx_and_order(blk->idx, order);
            blk->status = BLOCK_RESERVED;
        }
        
       
        
        buddy_blk->prev = buddy_blk->next = NULL;
        if(buddy_blk->status == BLOCK) push_to_freelist(order, buddy_blk);
        
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
            uart_puts(block_state_to_str(buddy_blk->status));
            uart_puts("\n");
            uart_puts("  buddy: addr = ");
            uart_put_hex((uintptr_t)buddy_blk->addr);
            uart_puts(", idx = ");
            uart_putint(buddy_blk->idx);
            uart_puts(", size = ");
            uart_putint(buddy_blk->size);
            uart_puts(", order = ");
            uart_putint(order);
            uart_puts("\n");
        }

        blk->status = BLOCK_RESERVED;
        set_entry_valid(blk, USED);
        
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
    
    buddy_block *block = lookup_alloc(addr);
    
    if (!block) {
        uart_puts("[free] ERROR: Unknown addr, cannot free\n");
        return;
    }
    int order = __builtin_ctz(block->size / PAGE_SIZE); // log
    if(block->addr != addr){
        uart_puts("Undefined behavior!");NL;
        return;
    }
    if(block->status != BLOCK_RESERVED) set_entry_valid(block, UNUSED);
    // buddy->alloc_table[block->idd].valid = UNUSED;

    if(DEBUG){
        uart_puts("[free] Start: addr = ");
        uart_put_hex((uintptr_t)addr);
        uart_puts(", idx = ");
        uart_putint(block->idx);
        uart_puts(", order = ");
        uart_putint(order);
        uart_puts("\n");
    }

    block->status = BLOCK;
    while (order < MAX_ORDER - 1) {
        int buddy_idx = find_buddy_index(block->idx, order);
        buddy_block *buddy_blk = get_block_by_page_idx_and_order(buddy_idx, order);

        if (!buddy_blk || (get_entry_valid(buddy_blk) == USED)) {
            // push_to_freelist(order, block);
            if(DEBUG){
                uart_puts("[free] Cannot merge with buddy idx = ");
                uart_putint(buddy_idx);
                uart_puts(" at order ");
                uart_putint(order);
                uart_puts(", buddy_blk status: ");
                uart_puts(block_state_to_str(buddy_blk->status));
                uart_puts(", buddy_blk valid: ");
                uart_puts(block_valid_to_str(get_entry_valid(buddy_blk)));
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
        
        // 自己和buddy都沒有用到就會到這
        buddy_block* parent_blk = get_parent_block(buddy_blk);
        buddy_blk->in_free_list = 0;
        block->in_free_list = 0;
        
        parent_blk->status = BLOCK;
        // parent_blk->in_free_list = 1;
        set_entry_valid(parent_blk, UNUSED);
        block = parent_blk;
        if (buddy_blk->prev) buddy_blk->prev->next = buddy_blk->next;
        if (buddy_blk->next) buddy_blk->next->prev = buddy_blk->prev;
        if (buddy->free_list[order] == buddy_blk){
            buddy->free_list[order] = buddy_blk->next;
            
        }
            

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
    disable_interrupts();
    int index = find_pool_index(size);
    memory_pool_t *pool;
    if (index == -1) {
        if(DEMO)
        uart_puts("[kmalloc] Size too large, use buddy\n");
        void* addr =  buddy_malloc(size, 0);
        enable_interrupts();
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
            enable_interrupts();
            return buddy_malloc(size, 0);
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
            enable_interrupts();
            return NULL;
        } 
            
        first_allocate_new_chunk = 1;
    }
    chunk_t *chunk = pool->free_list;
    
    pool->free_list = chunk->next;
    chunk_page_t* pg = pool->pages;
    uintptr_t page_base = (uintptr_t)chunk & ~(PAGE_SIZE - 1);
    if(!first_allocate_new_chunk){
        while(pg){
            if((uintptr_t)pg->page_addr == page_base){
                pg->free_chunks--;
                break;
            }
            
            pg = pg->next;
        }
        if(DEMO) print_free_list();
    }
    
    if(DEMO){
        print_pool_state();
        
    }
    enable_interrupts();
    return (void *)chunk;
}

void kfree(void *ptr) {
    disable_interrupts();
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t page_base = addr & ~(PAGE_SIZE - 1);
    
    if(DEMO){
        uart_puts("[kfree] ptr = ");
        uart_put_hex(addr); NL;
        uart_puts("[kfree] page base = ");
        uart_put_hex(page_base); NL;
    }
    

    buddy_block* block = lookup_alloc(ptr);
    if(block->status == BLOCK_RESERVED){
        if(DEMO)uart_puts("[kfree] Reserved memory! command cancel\n");
        enable_interrupts();
        return;
    }
    if (!block) {
        uart_puts("[kfree] block not found in alloc_table\n");
        enable_interrupts();
        return;
    }

    if (!block || block->status != BLOCK_CHUNK) {
        if(be_used((void*)page_base)){
            if(DEMO)uart_puts("Memory has not been allocated!\n");
            enable_interrupts();
            return;
        }
        if(DEMO)uart_puts("[kfree] → Not a chunk, calling buddy_free\n");
        buddy_free(ptr);
        if(DEMO) print_free_list();
        enable_interrupts();
        return;
    }
    
    if(DEMO)uart_puts("[kfree] → This is a CHUNK\n");

    memory_pool_t* pool;
    chunk_page_t* pg;
    int found = 0;
    for(int i = 0; i < NUM_POOLS; i++){
        pool = &mem_pools[i];
        for(chunk_page_t* tmp_pg = pool->pages; tmp_pg!=NULL; tmp_pg = tmp_pg->next){
            if((uintptr_t)tmp_pg->page_addr == page_base){
                pg = tmp_pg;
                found = 1;
                break;
            }
        }
        
        // for(int j = 0; j < MAX_PAGE_NUM_FOR_CHUNK; j++){
        //     pg = pool->pages + j;
        //     if(pool->chunk_size == 2048){
        //         uart_puts("free_pg_start: ");
        //         uart_put_hex(pg);NL;
        //         uart_put_hex(pg->page_addr);NL;
        //     }
            
        //     if((uintptr_t)pg->page_addr == page_base){
        //         found = 1;
        //         break;
        //     }
        // }
        if(found) break;
    }
    if(in_chunk_freelist(pool, ptr)){
        uart_puts("[kfree] The chunk has not been allocated.\n");
        enable_interrupts();
        return;
    }
    if(DEMO){
        uart_puts("[kfree] matched chunk size: ");
        uart_putint(pool->chunk_size); NL;
        uart_putint(found);NL;
    }

    chunk_page_t* prev = NULL;
    

    if (!found) {
        uart_puts("[kfree] cannot find chunk_page for addr\n");
        // break;
    }

    if(DEBUG || DEMO){
        uart_puts("[kfree] chunk page found → page addr: ");
        uart_put_hex((uintptr_t)pg->page_addr); NL;

        uart_puts("[kfree] After free chucnk: ");
        uart_putint(pg->total_chunks);
        uart_puts(", free_chunks: ");
        uart_putint(pg->free_chunks + 1); NL;
    }
    

    // 回收 chunk
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
        pool->num_page--;


        void* start = pg->page_addr;
        void* end = (void *)((char *)pg->page_addr + PAGE_SIZE);

        chunk_t* new_list = NULL;
        chunk_t* cur = pool->free_list;
        while (cur) {
            chunk_t* next = cur->next;
            if ((void *)cur < start || (void *)cur >= end) {
                cur->next = new_list;
                new_list = cur;
            }
            cur = next;
        }
        pool->free_list = new_list;


        if (prev) prev->next = pg->next;
        else pool->pages = pg->next;
        
        buddy_free(pg->page_addr);
        chunk_free = 1;
    }
    
    if(!chunk_free){ // chunk_free=1 代表有釋放page，所以有輸出過free_list
        if(DEBUG) print_free_list();
    }
    if(DEMO){
        print_pool_state();
        print_free_list();
        
    } 
    enable_interrupts();
    return;
}

// =================================== startup ===================================
void startup_allocator_init() {
    current = (uintptr_t)BUDDY_METADATA_ADDR;

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
            
            int block_size = (1 << order) * PAGE_SIZE;
            // 1. addr
            buddy->alloc_table[i].addr = (void *)((uintptr_t)(local_idx * block_size));

            // 2. block
            buddy->alloc_table[i].block = block_metadata_cursor;
            buddy->alloc_table[i].block->idx = local_idx * (1 << order);
            buddy->alloc_table[i].block->status = BLOCK;
            buddy->alloc_table[i].block->addr = (void *)((uintptr_t)(local_idx * block_size));
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
        mem_pools[i].pages = chunk_metadata_cursor + i * MAX_PAGE_NUM_FOR_CHUNK;
    }

    // memory_reserve((void*)0x00000000, (void*)0x00001);
    memory_reserve((void*)BUDDY_METADATA_ADDR, (void*)current);
    memory_reserve((void*)0x0000, (void*)0x1000);   // Spin tables
    memory_reserve((void*)_start,(void*) _end);     // Kernel image
    memory_reserve((void*)dtb_addr, (void*)dtb_addr + 60000); // Devicetree
    memory_reserve((void*)RAMDISK_BASE, (void*)RAMDISK_BASE + 30000); //Initramfs
    buddy_init();

}


void memory_reserve(void *start, void* end) {
    if (reserved_count < MAX_RESERVED) {
        reserved_ranges[reserved_count].start = (uintptr_t)start;
        reserved_ranges[reserved_count].end = (uintptr_t)end;
        reserved_count++;
    }else{
        uart_puts("[memory_reserve] Out of memeory\n");
    }
    uart_puts("\n\nreserve start address: ");
    uart_put_hex((uintptr_t)start);NL;
	uart_puts("reserve end address: ");
    uart_put_hex((uintptr_t)end);NL;
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
    blk->next = buddy->free_list[order];
    if (buddy->free_list[order]) {
        buddy->free_list[order]->prev = blk;
    }
    blk->prev = NULL;
    buddy->free_list[order] = blk;

    blk->status = BLOCK;  // 可用的 block，存的是 order
    blk->in_free_list = 1;
}

buddy_block *pop_from_freelist(int order) {
    buddy_block *blk = buddy->free_list[order];
    
    if (!blk) return NULL;

    buddy->free_list[order] = blk->next;
    if (blk->next) blk->next->prev = NULL;

    blk->next = blk->prev = NULL;
    blk->in_free_list = 0;
    return blk;
}

buddy_block *choose_from_freelist(int order, buddy_block* target_blk) {
    buddy_block *blk = buddy->free_list[order];
    if (!blk) return NULL;

    for(buddy_block* b = buddy->free_list[order]; b!=NULL; b = b->next){
        if(b->idd == target_blk->idd){
            b->in_free_list = 0;
            if(b->prev){
                b->prev->next = b->next;
                if(b->next) b->next->prev = b->prev;
                b->next = b->prev = NULL;
                return b;
            }else{
                buddy->free_list[order] = b->next;
                if(b->next) b->next->prev = NULL;
                b->next = b->prev = NULL;
                return b;
            }
            
        }
    }

    return NULL;
}

buddy_block* lookup_alloc(void* addr) {
    for(int order = 0; order < MAX_ORDER; order++){
        int block_size = (1 << order) * PAGE_SIZE;
        int local_block_index = (int)(uintptr_t)addr/block_size;
        // uart_puts("local: ");
        // uart_putint(local_block_index);
        // NL;
        int global_block_index = (1<<(MAX_ORDER-order-1))-1 + local_block_index;
        
        if(buddy->alloc_table[global_block_index].valid == USED){
            return buddy->alloc_table[global_block_index].block;
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
    

    // 新增 page metadata (之前已經有初始化過，設定值就好)
    chunk_page_t* new_pg = pool->pages + (int)pool->num_page;
    // uart_puts("metadata_addr ");
    // uart_put_hex(new_pg);NL;
    // uart_puts("pool->page_addr ");
    // uart_put_hex(pool->pages);NL;
    
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
    int local_block_idx = page_idx / (1 << order); // 每個order的blcock idx (base = 0)
    int level = MAX_ORDER - order - 1;
    return buddy->alloc_table[ (1<<level) - 1 + local_block_idx].block;
    
}

buddy_block* get_parent_block(buddy_block* blk){
    if(blk->idd == 0){ // root
        return NULL;
    }
    return buddy->alloc_table[(blk->idd-1)/2].block;
}

void set_entry_valid(buddy_block* blk, int val){
    buddy->alloc_table[blk->idd].valid = val;
}

int get_entry_valid(buddy_block* blk){
    return buddy->alloc_table[blk->idd].valid;
}

int in_chunk_freelist(memory_pool_t* m_pool, void* add){
    for(chunk_t* p = m_pool->free_list; p!= NULL; p=p->next){
        if(p == add){
            return 1;
        }
    }
    return 0;
}

int be_used(void* page_addr){
    uint32_t idx = (uint32_t)(uintptr_t)page_addr / PAGE_SIZE;
    buddy_block* blk = get_block_by_page_idx_and_order(idx, 0);
    for(uint32_t i = 0; i < MAX_ORDER; ++i){
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