// main.c
#include <gpio.h>
#include <uart.h>
#include <mailbox.h>
#include <framebuffer.h>
#include <cpio_.h>
#include <shell.h>
#include <allocator.h>
#include <device_tree.h>
#include <interrupt.h>
#include <exception_.h>
#include <task_queue.h>
#include <thread.h>

extern void* core_timer_enable;
void check_current_el(void) {
  unsigned long long el;
  asm volatile ("mrs %0, CurrentEL" : "=r" (el));  // 讀取 EL
  uart_puts("Current Exception Level (before fdt_traverse): ");
  uart_b2x_64(el >> 2);  // 右移 2 位，取得 EL 值
  uart_puts("\n");
}

void main() {
  uart_init();
  check_current_el();
  // asm volatile("bl core_timer_enable");
  fdt_traverse(initramfs_callback);
  
  // task_queue_init();
  uart_puts("Welcome to Simple Shell!\n");
  uart_puts("Type 'help' to see available commands.\n ");
  startup_allocator_init();
  task_queue_init();
  // print_pool_state();
  // print_free_list();

  uart_enable_interrupt();
  /* All handlers and queues are initialized before the CPU accepts IRQs. */
  irq_enable();
  
#if !ALLOC_DEMO
  DEMO = 0;
#endif
  uart_puts("# ");


  
  /*
  #define NUM_BLOCKS_AT_ORDER_0  2
  #define NUM_BLOCKS_AT_ORDER_1  4
  #define NUM_BLOCKS_AT_ORDER_2  4
  #define NUM_BLOCKS_AT_ORDER_3  5
  static void *ps0[NUM_BLOCKS_AT_ORDER_0];
  static void *ps1[NUM_BLOCKS_AT_ORDER_1];
  static void *ps2[NUM_BLOCKS_AT_ORDER_2];
  static void *ps3[NUM_BLOCKS_AT_ORDER_3];
  uart_putint(BUDDY_END/(1<<12));NL;
  // kmalloc(4096);

  for (uint64_t i = 0; i < NUM_BLOCKS_AT_ORDER_0 ; ++i) {
      // DEMO = 1;
      ps0[i] = kmalloc(4096);
  }
  for (uint64_t i = 0; i < NUM_BLOCKS_AT_ORDER_1; ++i) {
      ps1[i] = kmalloc(8192);
  }
  for (uint64_t i = 0; i < NUM_BLOCKS_AT_ORDER_2; ++i) {
      ps2[i] = kmalloc(16384);
  }
  for (uint64_t i = 0; i < NUM_BLOCKS_AT_ORDER_3; ++i) {
      ps3[i] = kmalloc(32768);
  }
  print_free_list();

#define  MAX_BLOCK_SIZE  (uint64_t)(PAGE_SIZE * (uint64_t)(1 << MAX_ORDER))


void* c1, *c2, *c3, *c4, *c5, *c6, *c7, *c8;
void *p1, *p2, *p3, *p4, *p5, *p6, *p7;
// DEMO = 1;
p1 = kmalloc(4095);
kfree(p1);                        // 4095
p1 = kmalloc(4095);
// print_free_list();
// print_pool_state();
c1 = kmalloc(1000);
c2 = kmalloc(1023);
c3 = kmalloc(999);
c4 = kmalloc(1010);
kfree(c3);                        // 999
// print_free_list();
// print_pool_state();
c5 = kmalloc(989);

c3 = kmalloc(88);
c6 = kmalloc(1001);
kfree(c3);                        // 88

uart_puts("line:\n");
c7 = kmalloc(2045);
c8 = kmalloc(1);


p2 = kmalloc(4096);
kfree(c8);                        // 1
p3 = kmalloc(16000);
kfree(p1);                        // 4095
kfree(c3);                        // 88
kfree(c7);                        // 2045
uart_puts("next");NL;

p4 = kmalloc(4097);
p5 = kmalloc(MAX_BLOCK_SIZE + 1);
p6 = kmalloc(MAX_BLOCK_SIZE);

kfree(p2);                        // 4096
kfree(p5);                        // MAX_BLOCK_SIZE + 1
kfree(p4);                        // 4097
p7 = kmalloc(7197);
DEMO = 0;
uart_puts("next2");NL;
kfree(p6);                        // MAX_BLOCK_SIZE
kfree(p3);                        // 16000
kfree(p7);                        // 7197
kfree(c1);                        // 1000
kfree(c6);                        // 1001
kfree(c2);                        // 1023
kfree(c5);                        // 989
kfree(c4);                        // 1010


uart_puts("\n-----------\n");

// Free all blocks remaining
for (int i = 0; i < NUM_BLOCKS_AT_ORDER_0; ++i) {
    kfree(ps0[i]);
}
for (int i = 0; i < NUM_BLOCKS_AT_ORDER_1; ++i) {
    kfree(ps1[i]);
}
for (int i = 0; i < NUM_BLOCKS_AT_ORDER_2; ++i) {
    kfree(ps2[i]);
}
for (int i = 0; i < NUM_BLOCKS_AT_ORDER_3; ++i) {
    kfree(ps3[i]);
}
*/

// uart_puts("\n==========\n");
  thread_init();
  if (!thread_create(kernel_shell_thread, NULL)) {
    uart_puts("Unable to create kernel shell thread\n");
    while (1)
      asm volatile("wfi" ::: "memory");
  }

  /* idle_task() is the one kernel event loop after boot: it schedules
   * threads, drains deferred IRQ work, and sleeps with wfi when idle. */
  idle_task();
}
