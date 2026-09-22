#include "lock.h"

/*
 * This project currently runs one active core.  Keep a nesting depth so a
 * nested lock does not re-enable IRQs belonging to an outer critical section,
 * and remember the state that existed before the first lock.
 */
static unsigned long long lock_count = 0;
static irq_state_t lock_saved_state = 0;

void lock(void){
    if (lock_count == 0)
        lock_saved_state = irq_save();
    else
        irq_disable();
    lock_count++;
}

void unlock(void){
    if (lock_count == 0) {
        uart_puts("Unlock without lock\n");
        while(1);
    }

    lock_count--;
    if(lock_count == 0)
        irq_restore(lock_saved_state);
}
