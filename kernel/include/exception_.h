# ifndef EXCEPTION_H
# define EXCEPTION_H
#include <interrupt.h>

#define CORE0_TIMER_IRQ_CTRL 0x40000040
#define IRQ_PENDING_1		(PERIPHERAL_BASE + 0x0000B204)
#define CORE0_INTERRUPT_SOURCE	0x40000060


#define CNTPSIRQ_BIT_POSITION 0x02
#define AUXINIT_BIT_POSTION 1<<29
extern int test_type;

typedef struct trap_frame{
  uint64_t x0;
  uint64_t x1;
  uint64_t x2;
  uint64_t x3;
  uint64_t x4;
  uint64_t x5;
  uint64_t x6;
  uint64_t x7;
  uint64_t x8;
  uint64_t x9;
  uint64_t x10;
  uint64_t x11;
  uint64_t x12;
  uint64_t x13;
  uint64_t x14;
  uint64_t x15;
  uint64_t x16;
  uint64_t x17;
  uint64_t x18; 
  uint64_t x19;
  uint64_t x20;
  uint64_t x21;
  uint64_t x22;
  uint64_t x23;
  uint64_t x24;
  uint64_t x25;
  uint64_t x26;
  uint64_t x27;
  uint64_t x28;
  uint64_t fp;           // x29
  uint64_t lr;           // x30
  uint64_t spsr_el1;     // origial process state
  uint64_t elr_el1;      // the address of the instruction that caused the exception(load it when returning to user program from EL1 to EL0)
  uint64_t sp_el0;       // stack pointer
}trap_frame_t;

void disable_interrupts();
void enable_interrupts();

// general
void exception_entry();
void irq_handler_function();

// system call
int c_system_call_handler(trap_frame_t *tf);

// timer
void core_timer_handler_function();
void timer_irq_handler_function();

// uart
void uart_enable_interrupt();
void uart_disable_interrupt();
void uart_irq_handler_function();
void uart_tx_handler();
void uart_rx_handler();

// test
void test_irq_handler();
void test_send_msg_irq_handler();
void delay_seconds(unsigned int seconds);

# endif