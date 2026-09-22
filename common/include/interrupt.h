# ifndef INTERRUPT_H
# define INTERRUPT_H

#include <gpio.h>
#include <stdint.h>

#define ENABLE_IRQS_1   ((volatile unsigned int*)(PERIPHERAL_BASE + 0x0000B210))
#define DISABLE_IRQS_1  ((volatile unsigned int*)(PERIPHERAL_BASE + 0x0000B21C))

/*
 * DAIF is the CPU's interrupt mask state.  A critical section should only
 * mask IRQs (DAIF.I, immediate value #2), not all four exception classes.
 * Keep the old disable_interrupts()/enable_interrupts() names for source
 * compatibility, but use irq_save()/irq_restore() whenever a function owns
 * a critical section.
 */
typedef uint64_t irq_state_t;

static inline irq_state_t irq_save(void)
{
    irq_state_t state;
    asm volatile(
        "mrs %0, daif\n"
        "msr daifset, #2\n"
        : "=r"(state)
        :
        : "memory");
    return state;
}

static inline void irq_restore(irq_state_t state)
{
    asm volatile("msr daif, %0" :: "r"(state) : "memory");
}

static inline void irq_disable(void)
{
    asm volatile("msr daifset, #2" ::: "memory");
}

static inline void irq_enable(void)
{
    asm volatile("msr daifclr, #2" ::: "memory");
}

#endif
