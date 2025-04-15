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


extern int ready_run_user; // define in user_prog.h. Set 1 when it ready to go to user space

int test_type = 0;


static inline void delay(int count) {
  while (count--) {
      asm volatile("nop");
  }
}

void disable_interrupts(){
  asm volatile(
    "msr daifset, #0xf;"
  );
}

void enable_interrupts(){
  asm volatile(
    "msr daifclr, #0xf;"
  );
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
  *(volatile unsigned int*)AUX_MU_IER_REG     |=   1;  //enable receive interrupt(transmit will be handled in its function)
  *(volatile unsigned int*)AUX_MU_IER_REG     |=   2;  //enable transmit interrupt
  *ENABLE_IRQS_1      |=   (1<<29);
}

void uart_disable_interrupt() {
    
  *(volatile unsigned int*)AUX_MU_IER_REG  = *(volatile unsigned int*)AUX_MU_IER_REG & ~(0x3);
  *DISABLE_IRQS_1      |=   (1<<29);
}

void irq_handler_function() {
  disable_interrupts();
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
      
      uint64_t spsr;
      asm volatile("mrs %0, spsr_el1" : "=r"(spsr));
      int prev_el = (spsr >> 2) & 0b11;
      // uart_puts("[TIMER IRQ]\n");
      // uart_putint(ready_run_user);
      // uart_puts("\n\n");
      if (prev_el == 0 || ready_run_user) {
        // uart_puts("[el0]\n");
        *(unsigned int*)CORE0_TIMER_IRQ_CTRL = 0; // clear
        task_enqueue(core_timer_handler_function, NULL, 1);
        // task_enqueue(timer_irq_handler_function, NULL, 1);
      } 
      else{
        // uart_puts("[el1]\n");
        *(unsigned int*)CORE0_TIMER_IRQ_CTRL = 0; // clear
        task_enqueue(timer_irq_handler_function, NULL, 1);
        

        // uint64_t freq;
        // asm volatile ("mrs %0, cntfrq_el0" : "=r" (freq));
        // asm volatile ("msr cntp_tval_el0, %0" :: "r" (2*freq));
        // *(unsigned int*)CORE0_TIMER_IRQ_CTRL = 2; // clear
      }
      

      // else {
      //     uart_puts("test0\n\n");
      //     // 忽略 timer 中斷（還是清掉，不然會一直觸發）
      //     // asm volatile ("msr cntp_tval_el0, %0" :: "r" (tval));
      //     *(unsigned int*)CORE0_TIMER_IRQ_CTRL = 0;
      //     task_enqueue(core_timer_handler_function, NULL, 1);
          
      // }
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
  // enable_interrupts();
  task_execute();
}

void uart_rx_handler()
{
    // uart_puts("rx");
    disable_interrupts();
    
    char c = *(volatile unsigned int*)AUX_MU_IO_REG & 0xFF;
    // mmio_read(AUX_MU_IO_REG) & 0xFF;

    // 收進 RX buffer
    uart_rx_buffer[uart_rx_tail++] = c;

    if (c == '\r' || c == '\n') {
        uart_tx_buffer[uart_tx_tail++] = '\r';
        uart_tx_buffer[uart_tx_tail++] = '\n';
        uart_rx_buffer[uart_rx_tail - 1] = '\0';
        
        *(volatile unsigned int*)AUX_MU_IER_REG |= 0x2;
        *(volatile unsigned int*)AUX_MU_IER_REG |= 0x1;
        enable_interrupts();
        delay(150);
        async_shell();
    } else {
        uart_tx_buffer[uart_tx_tail++] = c;
        *(volatile unsigned int*)AUX_MU_IER_REG |= 0x2;
        *(volatile unsigned int*)AUX_MU_IER_REG |= 0x1;
        enable_interrupts();
    }
    
   
}

void uart_tx_handler() {
  // uart_putint(uart_tx_head);
  // uart_puts(" ");
  // uart_putint(uart_tx_tail);
  // uart_puts("tx\n");
  disable_interrupts();
  // 如果沒資料就關掉 TX 中斷
  while (uart_tx_head != uart_tx_tail) {
    *(volatile unsigned int*)AUX_MU_IO_REG = uart_tx_buffer[uart_tx_head++];
    if (uart_tx_head >= UART_BUF_SIZE)
        uart_tx_head = 0;
  }
  uart_tx_head = 0;
  uart_tx_tail = 0;
  *(volatile unsigned int*)AUX_MU_IER_REG &= ~0x2;
  enable_interrupts();
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
  disable_interrupts();
  
  //enable core_0_timer
  *(unsigned int*)CORE0_TIMER_IRQ_CTRL = 2;

  asm volatile("msr cntp_ctl_el0,%0"::"r"(0));
  

  uint64_t current_time;
  asm volatile("mrs %0, cntpct_el0":"=r"(current_time));

  while(timer_head && timer_head->expiry <= current_time) {
    timer_t *timer = timer_head;

    //Execute the callback
    timer->callback(timer->data);

    // Remove timer from the list
    timer_head = timer->next;
    if (timer_head) {
        timer_head->prev = NULL;
    }
    
    //free timer
    
    // Reprogram the hardware timer if there are still timers left
    if(timer_head) {
      asm volatile("msr cntp_cval_el0, %0"::"r"(timer_head->expiry));
      asm volatile("msr cntp_ctl_el0,%0"::"r"(1));
    } 
    // else {
    //   asm volatile("msr cntp_ctl_el0,%0"::"r"(0));
    // }
  

    //enable interrupt
    enable_interrupts();
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



