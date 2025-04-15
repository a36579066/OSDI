# ifndef INTERRUPT_H
# define INTERRUPT_H

#include <gpio.h>
#define ENABLE_IRQS_1   ((volatile unsigned int*)(PERIPHERAL_BASE + 0x0000B210))
#define DISABLE_IRQS_1  ((volatile unsigned int*)(PERIPHERAL_BASE + 0x0000B21C))



#endif 