#include <syscall.h>
#include <uart.h>
#include <thread.h>
#include <cpio_.h>
// trap_frame_t *current_tf;
 

int test_syscall(){
  // current_tf->x0 = current_task->pid; 
  // return current_task->pid;
  char* buf;
  while(1){
    char c = uart_recv();
    uart_putc(c);NL;
    uart_puts("hi\n");
  }
  
  return 0;
}


int getpid(){
  
  thread_t* t = (thread_t*)get_current(); 
  // uart_puts("pid: ");
  // uart_putint(t->pid);NL;
  return t->pid;
}

// kernel‐side UART write syscall
size_t sys_uart_write(const char *buf, size_t size) {
  // 現在沒有 MMU ，user address 和 kernel address 相同，直接讀
  for (size_t i = 0; i < size; i++) {
      uart_putc(buf[i]);
  }
  return size;
}

size_t sys_uart_read(char *buf, size_t size) {
  size_t i;
  for (i = 0; i < size; i++) {
      char c = uart_recv();  // 低階讀一字元（blocking）
      buf[i] = c;
      if (c == '\r' || c == '\n') {
          // 可選：把 '\r' 或 '\n' 放進 buffer 再 return
          i++;
          break;
      }
  }
  return i;
}


int sys_exec(const char* name, char *const argv[]){
  uart_puts(name);NL;
  struct cpio_file file = parse_or_find_file_in_archive((char*)name, 1);
  // indicating that the file is either a directory or not exist
  if (file.data == NULL) {
    uart_puts("[sys_exec] file not found");NL;
    return 0;  // 找不到
  }
  thread_t* cur_t = get_current();
  cur_t->data_size = file.size;
  kfree(cur_t->data);
  cur_t->data = (char*)kmalloc(cur_t->data_size);
  
  // memcpy_((void*)cur_t->data, (void*)file.data, file.size);
  for(int i = 0; i < file.size; i++)
    cur_t->data[i] = file.data[i];
  memcpy_(cur_t->trapframe, 0, sizeof(trap_frame_t));
  trap_frame_t* tf = cur_t->trapframe;
  tf->elr_el1 = (unsigned long)cur_t->data;
  tf->sp_el0 = (unsigned long)cur_t->context.sp;
  tf->spsr_el1 = 0;
  
  cur_t->context.sp = (uint64_t)tf;
  cur_t->context.lr = (uint64_t)tf->elr_el1;
  uart_puts("[sys_exec] mem: ");
  uart_put_hex(cur_t->context.lr);
  uart_puts("  thread mem: ");
  uart_put_hex(cur_t);NL;
  unsigned long spsr_el1 = 0x3c0;
	asm volatile ("msr spsr_el1, %0" ::"r"(spsr_el1));
  asm volatile("msr elr_el1,%0" ::"r"(cur_t->data));
	asm volatile("msr sp_el0,%0" :: "r"(cur_t->context.sp+10000));
  asm volatile("eret");
  // tf->x0 = 0;
  // schedule();
  // asm volatile ("msr spsr_el1, %0" ::"r"(1));
  return 0;
}

void jump_user_prog(void *target_addr, char *user_sp)
{
    asm volatile("mov x0, 0  \n");
    asm volatile("msr spsr_el1, x0   \n"); // daif=0
    asm volatile("msr elr_el1, %0    \n" ::"r"(target_addr));
    asm volatile("msr sp_el0, %0    \n" ::"r"(user_sp));
    
    asm volatile("eret   \n");
}



int sys_fork(trap_frame_t *tf) {
  // ------ 1) 新建 PCB/thread ------
  thread_t *child = thread_create(NULL, NULL);
  if (!child) return -1;
  thread_t* cur_t = (thread_t*)get_current();
  uart_puts("cur id: ");
  uart_putint(cur_t->pid);NL;
  // 若未来有 MMU，要复制 pagetable；裸机可先共用
  child->pagetable = cur_t->pagetable;
  
  // ------ 2) 在 child kernel-stack 頂端分配 trap_frame 并复制 ------
  uint64_t ksp = child->context.sp;
  child->trapframe = (trap_frame_t*)(ksp - sizeof(trap_frame_t));
  child->trapframe->x0 = cur_t->trapframe->x0;
  child->trapframe->x1 = cur_t->trapframe->x1;
  child->trapframe->x2 = cur_t->trapframe->x2;
  child->trapframe->x3 = cur_t->trapframe->x3;
  child->trapframe->x4 = cur_t->trapframe->x4;
  child->trapframe->x5 = cur_t->trapframe->x5;
  child->trapframe->x6 = cur_t->trapframe->x6;
  child->trapframe->x7 = cur_t->trapframe->x7;
  child->trapframe->x8 = cur_t->trapframe->x8;
  child->trapframe->x9 = cur_t->trapframe->x9;
  child->trapframe->x10 = cur_t->trapframe->x10;
  child->trapframe->x11 = cur_t->trapframe->x11;
  child->trapframe->x12 = cur_t->trapframe->x12;
  child->trapframe->x13 = cur_t->trapframe->x13;
  child->trapframe->x14 = cur_t->trapframe->x14;
  child->trapframe->x15 = cur_t->trapframe->x15;
  child->trapframe->x16 = cur_t->trapframe->x16;
  child->trapframe->x17 = cur_t->trapframe->x17;
  child->trapframe->x18 = cur_t->trapframe->x18;
  child->trapframe->x19 = cur_t->trapframe->x19;
  child->trapframe->x20 = cur_t->trapframe->x20;
  child->trapframe->x21 = cur_t->trapframe->x21;
  child->trapframe->x22 = cur_t->trapframe->x22;
  child->trapframe->x23 = cur_t->trapframe->x23;
  child->trapframe->x24 = cur_t->trapframe->x24;
  child->trapframe->x25 = cur_t->trapframe->x25;
  child->trapframe->x26 = cur_t->trapframe->x26;
  child->trapframe->x27 = cur_t->trapframe->x27;
  child->trapframe->x28 = cur_t->trapframe->x28;
  child->trapframe->fp = cur_t->trapframe->fp;
  child->trapframe->lr = cur_t->trapframe->lr;
  child->trapframe->spsr_el1 = cur_t->trapframe->spsr_el1;
  child->trapframe->elr_el1 = cur_t->trapframe->elr_el1;
  child->trapframe->sp_el0 = cur_t->trapframe->sp_el0;
  uart_putint(child->trapframe->sp_el0);NL;
  uart_putint(cur_t->trapframe->sp_el0);NL;

  // ------ 3) 为 child 分配新的stack 并copy parent stack内容 ------
  // 计算父进程“已用”栈大小（从 sp_el0 到 USER_STACK_TOP）
  // uint64_t old_sp   = tf->sp_el0;
  uint64_t old_sp   = cur_t->context.sp;
  size_t   used_sz  = (cur_t->sp + THREAD_STK_SIZE) - old_sp;

  // uart_putint(old_sp);NL;
  // uart_putint(cur_t->sp);NL;
  // 分配全新用户栈
  char *new_stack   = kmalloc(THREAD_STK_SIZE);
  
  // uart_putint((uintptr_t)new_stack);NL;
  // uart_putint((uintptr_t)new_stack + (THREAD_STK_SIZE));NL;
  // uart_putint((uintptr_t)new_stack + (THREAD_STK_SIZE - used_sz));NL;
  // uart_putint(used_sz);NL;

  for(int i = 0; i < THREAD_STK_SIZE; i++){
    *((char*)child->sp + i) = *((char*)cur_t->sp + i);
  }

  child->trapframe->sp_el0 = (uint64_t)(new_stack + cur_t->trapframe->sp_el0 - cur_t->sp);
  child->trapframe->fp = (uint64_t)(new_stack + cur_t->trapframe->sp_el0 - cur_t->sp);
  child->sp = (uint64_t)new_stack;
  
  // 计算 child 的新 sp_el0
  child->trapframe->sp_el0 = (uint64_t)new_stack + (THREAD_STK_SIZE - used_sz);

  // ------ 4) 区分 fork 的返回值 ------
  child->trapframe->x0 = 0;           // child 里 fork() 返回 0
  tf->x0               = child->pid;  // parent 里 fork() 返回 child pid
  
  // ------ 5) 设置 child 的 context，让它下次 eret_to_user ------
  child->context.sp = (uint64_t)child->trapframe;
  child->context.lr = (uint64_t)ret_from_fork;
  
  // ------ 6) 把 child 丢到就绪队列 ------
  lock();
  if (!run_queue) {
      run_queue = child->next = child->prev = child;
  } else {
      thread_t *tail = run_queue->prev;
      tail->next    = child;
      child->prev   = tail;
      child->next   = run_queue;
      run_queue->prev = child;
  }
  child->status = THREAD_READY;
  unlock();
  uart_puts("child pis: ");
  uart_putint(child->pid);NL;
  if(cur_t->pid==1)
  schedule();
  return child->pid;
}



int fork(trap_frame_t *tf)
{
  
    disable_interrupts();
    // 1. 分配并浅拷贝父线程结构
    thread_t *child = (thread_t*)kmalloc(sizeof(thread_t));
    if (!child) {
        enable_interrupts();
        return NULL;
    }
    
    memcpy_((void*)child, (void*)cur_thread,sizeof(thread_t) );
    // *child = *cur_thread;        // copy context, sp, data, data_size, pid, trapframe, …
    // uart_puts("hi");
    child->pid    = next_pid++;  // 分配新 PID
    child->status = THREAD_READY;
    child->trapframe = NULL;     // 稍后重新建立
    enable_interrupts();
    
    // 2. 克隆用户栈：用 thread->data / data_size 字段存放用户栈
    child->data_size = THREAD_STK_SIZE;
    child->data = kmalloc(THREAD_STK_SIZE);
    if (!child->data) uart_puts("no user stack for child");
    // 计算父线程用户栈底
    unsigned long parent_stack_top = tf->sp_el0;
    unsigned long parent_stack_bot = parent_stack_top - THREAD_STK_SIZE;
    memcpy_(child->data,
           (void*)parent_stack_bot,
           THREAD_STK_SIZE);

    // 3. 分配内核栈，并在顶端铺设一个新的 TrapFrame
    child->sp = kmalloc(THREAD_STK_SIZE);
    if (!child->sp) uart_puts("no kernel stack for child");
    // TrapFrame 放在栈顶
    child->trapframe = (trap_frame_t*)
       ((unsigned long)child->sp + THREAD_STK_SIZE - sizeof(trap_frame_t));
    memcpy_(child->trapframe,
           tf,
           sizeof(trap_frame_t));
  
    // 4. 调整 child 的 TrapFrame
    // 子进程 fork() 返回值设为 0
    child->trapframe->x0 = 0;
    // 指向 child 的新用户栈顶（并确保 16 字节对齐）
    unsigned long child_sp_el0 = (unsigned long)child->data + THREAD_STK_SIZE;
    child_sp_el0 &= ~0xFULL;  // 按需对齐
    child->trapframe->sp_el0 = child_sp_el0;
    // elr_el1 保持原位（从哪指令返回就继续执行哪条）
    // child->trapframe->elr_el1 = tf->elr_el1;

    // 5. 设定 child 的内核 context，让 schedule 时跳到 eret_to_user
    child->context.sp = (unsigned long)child->trapframe;
    child->context.lr = (unsigned long)eret_to_user;

    // 6. 插入到 run_queue
    lock();
    if (!run_queue) {
        run_queue = child->next = child->prev = child;
    } else {
        thread_t *tail = run_queue->prev;
        tail->next       = child;
        child->prev      = tail;
        child->next      = run_queue;
        run_queue->prev  = child;
    }
    unlock();
    
    return child->pid;
}

