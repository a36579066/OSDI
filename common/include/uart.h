#ifndef UART_H
#define UART_H

#include <gpio.h>
#include <interrupt.h>


#define AUX_BASE        (PERIPHERAL_BASE + 0x00215000)
#define AUX_ENABLES     (AUX_BASE + 0x4)
#define AUX_MU_IO_REG   (AUX_BASE + 0x40)
#define AUX_MU_IER_REG  (AUX_BASE + 0x44)
#define AUX_MU_IIR_REG  (AUX_BASE + 0x48)
#define AUX_MU_LCR_REG  (AUX_BASE + 0x4C)
#define AUX_MU_MCR_REG  (AUX_BASE + 0x50)
#define AUX_MU_LSR_REG  (AUX_BASE + 0x54)
#define AUX_MU_CNTL_REG (AUX_BASE + 0x60)
#define AUX_MU_BAUD_REG (AUX_BASE + 0x68)

#define UART_BUF_SIZE 128

extern char uart_rx_buffer[UART_BUF_SIZE];
extern int uart_rx_head;
extern int uart_rx_tail;

extern char uart_tx_buffer[UART_BUF_SIZE];
extern int uart_tx_head;
extern int uart_tx_tail;

extern char shell_input_buffer[UART_BUF_SIZE];
extern int shell_input_tail;



void uart_init    ();
void uart_send    (unsigned char c);
void uart_gets(char* buffer, unsigned int max_len);
char uart_recv    ();
void uart_puts    (const char* str);
//void uart_putint(int num);
void uart_putint(unsigned long long num);
void uart_put_hex(unsigned int value);

void uart_putc(unsigned char c);
void uart_b2x_64(unsigned long long num);
unsigned char uart_recv_img();


#endif