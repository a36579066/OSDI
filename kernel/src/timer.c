#include <timer.h>
#include <exception_.h>
#include <uart.h>
#include <allocator.h>
#include <string.h>

// 觸發中斷時間
// void time_iqr_trigger(uint32_t second){
//   uint64_t freq;
//   asm volatile ("mrs %0, cntfrq_el0" : "=r" (freq));
//   uint64_t tval = second * freq;
//   asm volatile ("msr cntp_tval_el0, %0" :: "r" (tval));
//   *(unsigned int*)CORE0_TIMER_IRQ_CTRL = 2;
// }

timer_t *timer_head = NULL;
//  timer insert 到 linklist
static void insert_timer(timer_t *new_timer) {
  if (!timer_head || new_timer->expiry < timer_head->expiry) {
      new_timer->next = timer_head;
      new_timer->prev = NULL;
      if (timer_head)
          timer_head->prev = new_timer;
      timer_head = new_timer;
      asm volatile("msr cntp_cval_el0, %0" :: "r"(new_timer->expiry));
      asm volatile("msr cntp_ctl_el0, %0" :: "r"(1));
  } else {
      timer_t *current = timer_head;
      while (current->next && current->next->expiry < new_timer->expiry)
          current = current->next;
      new_timer->next = current->next;
      new_timer->prev = current;
      if (current->next)
          current->next->prev = new_timer;
      current->next = new_timer;
  }
}

static void schedule_timer(timer_callback callback, void *data, uint64_t seconds) {
  // timer_t *new_timer = (timer_t *)simple_malloc(sizeof(timer_t));
  timer_t *new_timer = (timer_t *)kmalloc(sizeof(timer_t));
  if (!new_timer)
      return;
  new_timer->callback = callback;
  new_timer->data = data;
  
  uint64_t current_time, cntfrq;
  asm volatile("mrs %0, cntpct_el0" : "=r"(current_time));
  asm volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq));
  new_timer->expiry = current_time + seconds * cntfrq;
  
  irq_state_t state = irq_save();
  insert_timer(new_timer);
  irq_restore(state);
}

static void print_message(void *data) {
  char *message = (char *)data;
  uint64_t current_time, cntfrq;
  asm volatile("mrs %0, cntpct_el0" : "=r"(current_time));
  asm volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq));
  uart_puts("Timeout: ");
  uart_puts(message);
  uart_puts(" ( ");
  uart_putint(current_time / cntfrq);
  uart_puts(" s)");
  uart_puts("\n");
}

void setTimeout(char *message, uint64_t seconds) {
  size_t len = strlen_(message) + 1;
  // char *msg_copy = (char *)simple_malloc(len);
  char *msg_copy = (char *)kmalloc(len);
  if (!msg_copy)
      return;
  strcpy_(msg_copy, message);
  
  if (!timer_head) {
      unsigned int value = 2;
      unsigned int *address = (unsigned int *)CORE0_TIMER_IRQ_CTRL;
      *address = value;
  }
  
  schedule_timer(print_message, msg_copy, seconds);
}
