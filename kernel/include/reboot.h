#ifndef REBOOT_H
#define REBOOT_H

#include <memory.h>

// #define PM_RSTC    ((volatile unsigned int*)0x3F10001C)  // Reset Control
// #define PM_WDOG    ((volatile unsigned int*)0x3F100024)  // Watchdog Timer
// #define PM_PASSWORD 0x5A000000
// #define PM_RSTC_RESET 0x00000020  // Full reset

void reboot();


#define PM_PASSWORD 0x5a000000
#define PM_RSTC PHYS_TO_KVA(0x3F10001cUL)
#define PM_WDOG PHYS_TO_KVA(0x3F100024UL)
void set(long addr, unsigned int value);
void reset(int tick);
#endif
