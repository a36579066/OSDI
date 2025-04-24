#ifndef ALLOCATOR_H
#define ALLOCATOR_H
#include "type.h"


#define PAGE_SIZE        (uint64_t)4096             // 一頁大小（4KB）
#define MAX_ORDER        19               // 最大階層 (0x3c000000/0x1000 = 0x3c000 = 245760 = 2^17 + 2^16 + 2^15 + 2^14) => 19
#define BUDDY_START      0x0       // buddy 系統管理的起始實體記憶體位址
#define BUDDY_END        0x3c000000       // buddy 系統管理的結束實體記憶體位址
#define BUDDY_METADATA_ADDR  0x10000000   // metadata 存放處（不要放在可分配區）
#define MAX_ALLOC (1 << MAX_ORDER) - 1
#define MAX_PAGE_NUM_FOR_CHUNK 1
#define NUM_POOLS        6


#define NUM_POOLS 6
static const size_t pool_sizes[NUM_POOLS] = {16, 32, 48, 64, 96, 1024};


#define MAX_RESERVED 32

extern int DEBUG;
extern int DEMO;
typedef struct chunk {
    struct chunk* next;
} chunk_t;


typedef struct chunk_page {
  void* page_addr;
  int total_chunks;
  int free_chunks;
  struct chunk_page* next;
  
} chunk_page_t;

typedef struct memory_pool {
  size_t chunk_size;
  size_t num_page;
  chunk_t* free_list;
  chunk_page_t* pages; // 管理所有分配出去的 chunk page
  
} memory_pool_t;
// 總共有 NUM_POOLS 個 memory_pool_t


extern char _heap_start;
extern char _heap_end;
// extern char _start;
// extern char _end;

typedef struct buddy_block_list {
  // int idd;                    // unique block id
  int idx;                   // block 的 global index (以 page 為單位)
  int status;                   // block 狀態：
  struct buddy_block_list *prev;
  struct buddy_block_list *next;
  void *addr;                // 實體記憶體位置
  unsigned int size;        // block 大小（bytes）
  int idd;
  int in_free_list;         // 0:not, 1:yes
} buddy_block;


typedef struct {
  uintptr_t* addr;
  buddy_block* block;
  int valid; 
} allocation_entry;


typedef struct buddy_system_struct {
  // buddy_block *free_list[MAX_ORDER];
  // allocation_entry alloc_table[MAX_ALLOC];
  buddy_block **free_list;
  allocation_entry *alloc_table;
  // memory_pool_t* mem_pools;
} buddy_system;

typedef enum {
  BLOCK_ALLOCATED = -1,  // 已被分配（或保留）
  BLOCK_RESERVED = -2,     // 尚未使用
  BLOCK_SPLIT = -3,      // 被切成更小的 block，不可再用
  BLOCK_CHUNK = -4,      // 被切成chunk
  BLOCK = -5
  // val >= 0: 表示該 block 是可用的，值就是它的 order
} buddy_block_state;

typedef enum {
  UNUSED = -6,     // 已被分配（或保留）
  USED = -7,     // 尚未使用
  // val >= 0: 表示該 block 是可用的，值就是它的 order
} allocation_entry_state;


typedef struct {
  uintptr_t start;
  uintptr_t end;
} reserved_range_t;

void startup_allocator_init();
void* reserve_startup_allocations(unsigned int size, buddy_block* target_blk);
void* test(int idx);
void memory_reserve(void *start, void* end);
// void *startup_alloc(size_t size);
// void record_reserved(void *start, size_t size);
// void reserve_startup_allocations();


void* simple_malloc(size_t size);
void buddy_init();
void* buddy_malloc(unsigned int size, int chunk);
buddy_block* buddy_split(int start_index, int req_size, int order);
void buddy_free(void *addr); // 還沒實作free空指標的例外


int find_pool_index(size_t size);
void* allocate_new_chunk_page(memory_pool_t *pool);
void* kmalloc(size_t size);
void kfree(void *ptr);

void print_free_list();
void print_alloc();
void print_pool_state();

#endif