#include <task_queue.h>
#include <exception_.h>
#include <allocator.h>
#include <uart.h>
#include <type.h>


static task_t *task_head = 0;
static task_t *task_tail = 0;
task_t* task_start_addr = 0;

/* =============== tools =============== */
task_t* set_task(task_callback_t callback, void *data, int priority);
void clean_task(task_t* task);
/* ===================================== */

void task_queue_init(void) {
    disable_interrupts();
    task_head = 0;
    task_tail = 0;
    DEMO = 0;
    
    task_start_addr = (task_t *)kmalloc(sizeof(task_t)*MAX_TASKS);
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

    DEMO = 0;
    enable_interrupts();
}


int task_enqueue(task_callback_t callback, void *data, int priority) {
  disable_interrupts();
  // task_t* new_task = (task_t *)simple_malloc(sizeof(task_t));
  task_t* new_task = set_task(callback, data, priority);
  
  if(new_task == NULL){
    uart_puts("Too many interrupt! At most ");uart_putint(MAX_TASKS);uart_puts(" tasks\n");
    return 0;
  }


  if(new_task == 0) {
      uart_puts("Out of memory");NL;
      enable_interrupts();
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
  enable_interrupts();
  return 1;
}


void task_execute(void) {
    task_t *current = 0;
    while(1) {
        disable_interrupts();
        if(task_head == 0) {
            enable_interrupts();
            break;
        }

        current = task_head;
        task_head = current->next;
        if(task_head == 0)
            task_tail = 0;
        task_callback_t callback = current->callback;
        clean_task(current);
        enable_interrupts(); // 隨時可以插入新task
        // current->callback();
        callback();
        
        // enable_interrupts();
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