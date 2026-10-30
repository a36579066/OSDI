#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>
#include <memory.h>

#define PERIPHERAL_PHYS_BASE 0x3F000000UL
#define PERIPHERAL_BASE PHYS_TO_KVA(PERIPHERAL_PHYS_BASE)
#define GPIO_BASE       (PERIPHERAL_BASE + 0x200000)

#define GPFSEL1         (PERIPHERAL_BASE + 0x00200004)
#define GPPUD           (PERIPHERAL_BASE + 0x00200094)
#define GPPUDCLK0       (PERIPHERAL_BASE + 0x00200098)

// static inline void mmio_write(unsigned int reg, unsigned int val) ;
// static inline unsigned int mmio_read(unsigned int reg) ;
// static inline void delay(int count) ;
// void gpio_set_uart_pins(void) ;
void mmio_write(uintptr_t reg, unsigned int val);
unsigned int mmio_read(uintptr_t reg);

#endif
