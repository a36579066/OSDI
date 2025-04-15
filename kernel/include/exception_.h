# ifndef EXCEPTION_H
# define EXCEPTION_H
#include <interrupt.h>

#define CORE0_TIMER_IRQ_CTRL 0x40000040
#define IRQ_PENDING_1		(PERIPHERAL_BASE + 0x0000B204)
#define CORE0_INTERRUPT_SOURCE	0x40000060


#define CNTPSIRQ_BIT_POSITION 0x02
#define AUXINIT_BIT_POSTION 1<<29
extern int test_type;


void disable_interrupts();
void enable_interrupts();

// general
void exception_entry();
void irq_handler_function();

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