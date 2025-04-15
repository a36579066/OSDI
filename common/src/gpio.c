#include <gpio.h>
#include <stdint.h> 


void mmio_write(uintptr_t reg, unsigned int val) {
  *(volatile unsigned int*)reg = val;
}

unsigned int mmio_read(uintptr_t reg) {
  return *(volatile unsigned int*)reg;
}

static inline void delay(int count) {
  while (count--) {
      asm volatile("nop");
  }
}

void gpio_set_uart_pins(void) {
  // 1. set GPIO 14 (TXD1) and  GPIO 15 (RXD1) to ALT5 (Mini UART)
  unsigned int selector = mmio_read(GPFSEL1);
  selector &= ~((7 << 12) | (7 << 15)); // clear GPIO14  and  GPIO15
  selector |=  (2 << 12) | (2 << 15);   // set ALT5 (010)
  mmio_write(GPFSEL1, selector);

  // 2. close GPIO14 and GPIO15 (Pull-up/down)
  mmio_write(GPPUD, 0);  // forbidon Pull-up/down
  delay(150);            // (P.101)

  mmio_write(GPPUDCLK0, (1 << 14) | (1 << 15)); //  GPIO14  GPIO15
  delay(150);            

  mmio_write(GPPUDCLK0, 0); 
}
