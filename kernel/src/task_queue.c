#include <task_queue.h>
#include <exception_.h>
#include <allocator.h>


static task_t *task_head = 0;
static task_t *task_tail = 0;



void task_queue_init(void) {
    disable_interrupts();
    task_head = 0;
    task_tail = 0;
    enable_interrupts();
}


int task_enqueue(task_callback_t callback, void *data, int priority) {
  disable_interrupts();
  task_t *new_task = (task_t *)simple_malloc(sizeof(task_t));
  if(new_task == 0) {
      enable_interrupts();
      return 0;
  }
  
  new_task->callback = callback;
  new_task->data = data;
  new_task->priority = priority;
  new_task->next = 0;
  new_task->prev = 0;
  
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
        enable_interrupts(); // 隨時可以插入新task
        current->callback();
        // enable_interrupts();
  }
}
