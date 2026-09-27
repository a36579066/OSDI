#include <exception_.h>
#include <uart.h>
#include <stdint.h>
#include <gpio.h>
#include <interrupt.h>
#include <shell.h>
#include <timer.h>
#include <task_queue.h>
#include <utils.h>
#include <user_prog.h>
#include <allocator.h>
#include <syscall.h>


extern int ready_run_user; // define in user_prog.h. Set 1 when it ready to go to user space

int test_type = 0;


static inline void delay(int count) {
  while (count--) {
      asm volatile("nop");
  }
}

void disable_interrupts(){
  irq_disable();
}

void enable_interrupts(){
  irq_enable();
}

trap_frame_t *c_system_call_handler(trap_frame_t *tf) {
  if (!tf)
    return tf;

  uint64_t num = tf->x8;
  // In this QEMU exception path ELR_EL1 already points to the instruction
  // after SVC.  Do not advance it again, or the resumed user code skips one
  // instruction after every syscall.
  switch (num) {
    case 0:
      tf->x0 = (uint64_t)getpid();
      break;
    case 1:
      tf->x0 = (uint64_t)sys_uart_read((char *)tf->x0,
                                       (size_t)tf->x1);
      break;
    case 2:
      tf->x0 = (uint64_t)sys_uart_write((const char *)tf->x0,
                                        (size_t)tf->x1);
      break;
    case 3:
      tf->x0 = (uint64_t)sys_exec(tf, (const char *)tf->x0,
                                  (char *const *)tf->x1);
      break;
    case 4:
      tf->x0 = (uint64_t)sys_fork(tf);
      break;
    case 5:
      return exit_user_session(tf, (int)tf->x0);
    case 6:
      tf->x0 = (uint64_t)sys_mbox_call((unsigned char)tf->x0,
                                       (unsigned int *)tf->x1);
      break;
    case 7:
      tf->x0 = (uint64_t)sys_kill((int)tf->x0);
      break;
    case 8:
      tf->x0 = (uint64_t)sys_signal((int)tf->x0, tf->x1);
      break;
    case 9:
      tf->x0 = (uint64_t)sys_signal_kill((int)tf->x0, (int)tf->x1);
      break;
    case NR_SIGRETURN:
      return sys_sigreturn(tf);
    default:
      tf->x0 = (uint64_t)-1;
      break;
  }

  // Keep IRQs masked until load_all restores the user PSTATE from this frame.
  irq_disable();
  return tf;
}



void exception_entry() {
  unsigned long spsr, elr, esr, cur_el;
  asm volatile(
      "mrs %0, spsr_el1\n\t"
      "mrs %1, elr_el1\n\t"
      "mrs %2, esr_el1\n\t"
      "mrs %3, CurrentEL\n\t"
      : "=r"(spsr), "=r"(elr), "=r"(esr), "=r"(cur_el)
  );

  // uart_puts("Entering exception handler\n");
  // uart_puts("Current EL: \t");
  // uart_b2x_64(cur_el >> 2);   // EL 在 bits[3:2]
  // uart_puts("\nSPSR_EL1: \t");
  // uart_b2x_64(spsr);
  // uart_puts("\nELR_EL1: \t");
  // uart_b2x_64(elr);
  // uart_puts("\nESR_EL1: \t");
  // uart_b2x_64(esr);
  // uart_puts("\n");

  // uart_puts("Leaving exception handler\n");
}

void uart_enable_interrupt() {
  // TX is enabled only while uart_rx_handler has data to flush.  Enabling it
  // permanently creates an interrupt storm on an empty FIFO.
  *(volatile unsigned int*)AUX_MU_IER_REG &= ~2U;
  *(volatile unsigned int*)AUX_MU_IER_REG |= 1U;
  *ENABLE_IRQS_1      |=   (1<<29);
}

void uart_disable_interrupt() {
    
  *(volatile unsigned int*)AUX_MU_IER_REG  = *(volatile unsigned int*)AUX_MU_IER_REG & ~(0x3);
  *DISABLE_IRQS_1      |=   (1<<29);
}

/*
 * RX/TX sources stay masked until their deferred callback has consumed the
 * current device state.  Re-enabling RX in irq_handler_function() would
 * repeatedly enqueue uart_rx_handler while the same byte was still in the
 * FIFO.  The callback owns the re-arm point instead.
 */
static void uart_rearm_deferred_sources(void)
{
  if (ready_run_user)
    return;

  irq_state_t state = irq_save();
  unsigned int enable_mask = 1U; /* RX */
  if (uart_tx_head != uart_tx_tail)
    enable_mask |= 2U;            /* TX */
  *(volatile unsigned int *)AUX_MU_IER_REG |= enable_mask;
  irq_restore(state);
}

/* A completed command must keep RX masked until kernel_shell_thread() has
 * copied uart_rx_buffer.  TX may still be armed to flush the echoed line. */
static void uart_rearm_tx_only(void)
{
  irq_state_t state = irq_save();
  if (uart_tx_head != uart_tx_tail)
    *(volatile unsigned int *)AUX_MU_IER_REG |= 2U;
  irq_restore(state);
}

static void rearm_preemption_timer(void) {
  uint64_t freq;
  asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
  uint64_t delta = freq >> 5;
  if (delta == 0)
    delta = 1;
  asm volatile("msr cntp_tval_el0, %0" :: "r"(delta));
  asm volatile("msr cntp_ctl_el0, %0" :: "r"((uint64_t)1));
  *(volatile unsigned int *)CORE0_TIMER_IRQ_CTRL = 2;
}

trap_frame_t *irq_handler_function(trap_frame_t *tf) {
  if (!tf)
    return tf;

  /* The IRQ vector normally arrives with IRQs masked.  Keep the handler
   * masked while inspecting and acknowledging the hardware sources. */
  irq_disable();
  uint32_t irq_pending1 = *(volatile unsigned int*)IRQ_PENDING_1;
  uint32_t iir = *(volatile unsigned int*)AUX_MU_IIR_REG;
  uint32_t core_irq_source = *(volatile uint32_t*)(CORE0_INTERRUPT_SOURCE);
  
  // Timer Interrupt
  if (core_irq_source & (1 << 1)) {
      // unsigned int* address = (unsigned int*)CORE0_TIMER_IRQ_CTRL;
      // *address = 0;
      // timer_irq_handler_function();
      // task_enqueue(test_irq_handler, NULL, 1);
      // task_enqueue(core_timer_handler_function, NULL, 1);
      // task_enqueue(timer_irq_handler_function, NULL, 1);
      
      *(volatile unsigned int*)CORE0_TIMER_IRQ_CTRL = 0;

      if (cur_thread && cur_thread->is_user) {
        rearm_preemption_timer();

        // User processes are switched by replacing the frame that the
        // assembly epilogue will restore.  This avoids enqueueing one task
        // per timer tick and is safe even when the user is blocked in
        // uart_read.
        return schedule_from_irq(tf);
      }

      /* In the kernel shell, the same physical timer is used by setTimeout
       * with an absolute compare value.  Do not overwrite it with the user
       * preemption interval; service the timer list instead. */
      timer_irq_handler_function();

  }

  if (irq_pending1 & (1<<29)) {
      // RX 中斷
      if (iir & 0x04) { // bit 2 = on
          // 關 RX interrupt
          *(volatile unsigned int*)AUX_MU_IER_REG &= ~(0x1);
          task_enqueue(uart_rx_handler, NULL, 5);
          if(test_type == 1) {
            task_enqueue(test_irq_handler, NULL, 0);
            // task_enqueue(test_irq_handler, NULL, 0);
          }else if(test_type == 2){
            task_enqueue(test_send_msg_irq_handler, NULL, 1);
          }
          
          // uart_rx_handler();
          // 開 RX interrupt
          // *(volatile unsigned int*)AUX_MU_IER_REG |= 0x1;
      }
      // TX 中斷
      else if (iir & 0x02) { // bit 1 = on
        
        *(volatile unsigned int*)AUX_MU_IER_REG &= ~(0x2);
        // uart_tx_handler();
        task_enqueue(uart_tx_handler, NULL, 4);
      }
  }
  /*
   * The IRQ handler only acknowledges the device and queues deferred work.
   * task_execute() runs from the kernel event loop, outside this exception
   * frame, so callbacks may safely allow timer IRQs without recursively
   * entering the exception handler.
   */
  /* Do not enable IRQs here.  load_all/eret restores tf->spsr_el1. */
  return tf;
}

void uart_rx_handler()
{
    int line_ready = 0;
    irq_state_t state = irq_save();
    /*
     * A single host write can put several bytes in the Mini UART FIFO.  Drain
     * the FIFO in one deferred callback; otherwise re-enabling RX after only
     * one byte lets a level-triggered IRQ starve the normal event loop.
     * Stop at the first newline so kernel_shell_thread() consumes exactly
     * one line after it is woken.
     */
    while (!line_ready &&
           (*(volatile unsigned int *)AUX_MU_LSR_REG & 0x01U)) {
        char c = *(volatile unsigned int *)AUX_MU_IO_REG & 0xFF;

        if (!((c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              (c == ' ') || (c == '\b') || (c == '\r') ||
              (c == '\n') || (c == 0x7F) || (c == '\t') ||
              (c >= 0x21 && c <= 0x7E)))
            continue;

        /* Keep the buffers bounded; shell parsing uses a private snapshot. */
        if (c != 0x7F && uart_rx_tail < UART_BUF_SIZE - 1)
            uart_rx_buffer[uart_rx_tail++] = c;

        if (c == '\r' || c == '\n') {
            if (uart_tx_tail + 1 < UART_BUF_SIZE) {
                uart_tx_buffer[uart_tx_tail++] = '\r';
                uart_tx_buffer[uart_tx_tail++] = '\n';
            }
            if (uart_rx_tail > 0)
                uart_rx_buffer[uart_rx_tail - 1] = '\0';
            line_ready = 1;
        } else if (c != 0x7F && uart_tx_tail < UART_BUF_SIZE) {
            uart_tx_buffer[uart_tx_tail++] = c;
        }
    }

    irq_restore(state);

    if (line_ready) {
        async_shell_notify_line_ready();
        uart_rearm_tx_only();
        return;
    }

    uart_rearm_deferred_sources();
}

void uart_tx_handler() {
  // uart_putint(uart_tx_head);
  // uart_puts(" ");
  // uart_putint(uart_tx_tail);
  // uart_puts("tx\n");
  irq_state_t state = irq_save();
  // 如果沒資料就關掉 TX 中斷
  while (uart_tx_head != uart_tx_tail) {
    *(volatile unsigned int*)AUX_MU_IO_REG = uart_tx_buffer[uart_tx_head++];
    if (uart_tx_head >= UART_BUF_SIZE)
        uart_tx_head = 0;
  }
  uart_tx_head = 0;
  uart_tx_tail = 0;
  *(volatile unsigned int*)AUX_MU_IER_REG &= ~0x2;
  irq_restore(state);
}


static inline uint64_t get_cntpct(void) {
  uint64_t cnt;
  asm volatile("mrs %0, cntpct_el0" : "=r" (cnt));
  return cnt;
}

static inline uint64_t get_cntfrq(void) {
  uint64_t frq;
  asm volatile("mrs %0, cntfrq_el0" : "=r" (frq));
  return frq;
}

// 延遲指定秒數
void delay_seconds(unsigned int seconds) {
  uint64_t start = get_cntpct();
  uint64_t frq = get_cntfrq();
  while ((get_cntpct() - start) < seconds * frq);
}

// 模擬長時間中斷
void test_irq_handler() {
  test_type = 2;
  uart_puts("in test\n");
  delay_seconds(3);
  uart_puts("end test\n");
  test_type = 0;
  uart_rx_tail = 0;
  uart_tx_tail = 0;
  uart_tx_head = 0;
  test_type = 0;
}

void test_send_msg_irq_handler(){
  uart_puts("\n\nNested Interrupt\n\n");
  // test_type = 0;
}

void timer_irq_handler_function() {
  timer_t *expired_head = NULL;
  timer_t *expired_tail = NULL;
  uint64_t current_time;

  asm volatile("mrs %0, cntpct_el0" : "=r"(current_time));

  /* Only detach timer-list nodes while IRQs are masked.  Callbacks can print,
   * allocate, or enqueue work, so they must not run inside this critical
   * section. */
  irq_state_t state = irq_save();
  *(unsigned int*)CORE0_TIMER_IRQ_CTRL = 0;
  asm volatile("msr cntp_ctl_el0, %0" :: "r"((uint64_t)0));

  while (timer_head && timer_head->expiry <= current_time) {
    timer_t *timer = timer_head;
    timer_head = timer->next;
    if (timer_head)
      timer_head->prev = NULL;

    timer->next = NULL;
    timer->prev = expired_tail;
    if (expired_tail)
      expired_tail->next = timer;
    else
      expired_head = timer;
    expired_tail = timer;
  }

  if (timer_head) {
    asm volatile("msr cntp_cval_el0, %0" :: "r"(timer_head->expiry));
    asm volatile("msr cntp_ctl_el0, %0" :: "r"((uint64_t)1));
    *(unsigned int*)CORE0_TIMER_IRQ_CTRL = 2;
  }
  irq_restore(state);

  while (expired_head) {
    timer_t *timer = expired_head;
    expired_head = timer->next;
    if (timer->callback)
      timer->callback(timer->data);
    kfree(timer->data);
    kfree(timer);
  }
}

void core_timer_handler_function(void) {

  uint64_t count, freq;

  asm volatile ("mrs %0, cntpct_el0" : "=r" (count));
  asm volatile ("mrs %0, cntfrq_el0" : "=r" (freq));

  uint64_t seconds = count / freq;

  uart_puts("time: ");
  uart_putint(seconds);
  uart_puts(" s.\n");

  uint64_t tval = 2 * freq;
  asm volatile ("msr cntp_tval_el0, %0" :: "r" (tval));
  unsigned int* address = (unsigned int*)CORE0_TIMER_IRQ_CTRL;
  *address = 2;
}
