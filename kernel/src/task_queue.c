#include <task_queue.h>
#include <exception_.h>
#include <allocator.h>
#include <uart.h>
#include <type.h>


static task_t *task_head = 0;
static task_t *task_tail = 0;
static int task_queue_running = 0;
static int task_queue_overflowed = 0;
task_t* task_start_addr = 0;

/* =============== tools =============== */
task_t* set_task(task_callback_t callback, void *data, int priority);
void clean_task(task_t* task);
/* ===================================== */

void task_queue_init(void) {
    irq_state_t state = irq_save();
    task_head = 0;
    task_tail = 0;
    task_queue_running = 0;
    task_queue_overflowed = 0;
#if !ALLOC_DEMO
    DEMO = 0;
#endif
    
    task_start_addr = (task_t *)kmalloc(sizeof(task_t)*MAX_TASKS);
    if (task_start_addr == NULL) {
      irq_restore(state);
      uart_puts("[task_queue] allocation failed\n");
      return;
    }
    for(int i = 0; i < MAX_TASKS; ++i){
      task_t* task = task_start_addr + i;
      task->callback = NULL;
      task->data = NULL;
      task->priority = -1;
      task->used = 0;
      task->next = NULL;
      task->prev = NULL;
      task->addr = (void*) task;
      
    }

#if !ALLOC_DEMO
    DEMO = 0;
#endif
    irq_restore(state);
}


int task_enqueue(task_callback_t callback, void *data, int priority) {
  irq_state_t state = irq_save();
  // task_t* new_task = (task_t *)simple_malloc(sizeof(task_t));
  task_t* new_task = set_task(callback, data, priority);
  
  if(new_task == NULL){
    /* Never print from an IRQ path.  The event loop reports this once. */
    task_queue_overflowed = 1;
    irq_restore(state);
    return 0;
  }


  if(new_task == 0) {
      irq_restore(state);
      uart_puts("Out of memory");
      NL;
      return 0;
  }

  // 直接加到頭
  if(task_head == 0) {
      task_head = new_task;
      task_tail = new_task;
  } else {
      // 優先權高到低排序
      task_t *current = task_head;
      // cur priority 大於或等於新task的 priority，則繼續往後找
      while(current != 0 && current->priority >= priority) {
          current = current->next;
      }
      
      if(current == task_head) {
          // 優先權低於新任務，那新任務插入到開頭
          new_task->next = task_head;
          task_head->prev = new_task;
          task_head = new_task;
      } else if(current == 0) {
          // 插入在尾端
          new_task->prev = task_tail;
          task_tail->next = new_task;
          task_tail = new_task;
      } else {
          // 插到中間
          new_task->prev = current->prev;
          new_task->next = current;
          current->prev->next = new_task;
          current->prev = new_task;
      }
  }
  irq_restore(state);
  return 1;
}


void task_execute(void) {
    task_t *current = 0;

    /*
     * A UART IRQ may arrive while a callback is running.  It may enqueue
     * more work, but it must not recursively enter task_execute().
     */
    irq_state_t state = irq_save();
    if (task_queue_running) {
        irq_restore(state);
        return;
    }
    task_queue_running = 1;
    irq_restore(state);

    while(1) {
        int report_overflow;
        state = irq_save();
        report_overflow = task_queue_overflowed;
        task_queue_overflowed = 0;
        if(task_head == 0) {
            task_queue_running = 0;
            irq_restore(state);
            if (report_overflow) {
                uart_puts("Too many interrupt! At most ");
                uart_putint(MAX_TASKS);
                uart_puts(" tasks can be queued.\n");
            }
            break;
        }

        current = task_head;
        task_head = current->next;
        if(task_head == 0)
            task_tail = 0;
        else
            task_head->prev = 0;
        task_callback_t callback = current->callback;
        clean_task(current);
        irq_restore(state);

        if (report_overflow) {
            uart_puts("Too many interrupt! At most ");
            uart_putint(MAX_TASKS);
            uart_puts(" tasks can be queued.\n");
        }

        /*
         * Run callbacks outside the queue critical section.  This lets the
         * timer continue to run and lets a callback enqueue more work.  The
         * task_queue_running guard above prevents recursive queue draining.
         */
        if (callback != NULL)
            callback();
  }
}

task_t* set_task(task_callback_t callback, void *data, int priority){
  for(task_t* task = task_start_addr; task < task_start_addr + MAX_TASKS; task += 1){
    
    if(!task->used){
      task->callback = callback;
      task->data = data;
      task->priority = priority;
      task->used = 1;
      task->next = 0;
      task->prev = 0;
      task->addr = (void*) task;
      return task;
    }else{
      continue;
    }
  }
  return NULL;
}

void clean_task(task_t* task){
  task->callback = NULL;
  task->data = NULL;
  task->priority = -1;
  task->used = 0;
  task->next = NULL;
  task->prev = NULL;
  task->addr = (void*) task;
}
