#include "thread.h"

thread_t *cur_thread = NULL;
thread_t *run_queue = NULL;
int next_pid = 0;
static int num_thread = 0;

static inline void delay(int count) {
  while (count--) asm volatile("nop");
}

//------------------------------------------------------------------------------
// thread_init: 建立 idle task，並設為 current
//------------------------------------------------------------------------------
void thread_init(void) {
  lock();
  thread_t *idle = thread_create(idle_task, NULL);
  cur_thread = idle;
  // 把 cur_thread 放到 TPIDR_EL1，以便 exception handler 快取
  asm volatile("msr tpidr_el1, %0" :: "r"(idle));
  uart_puts("Thread system initialized.\n");
  unlock();
}

//------------------------------------------------------------------------------
// thread_create: 建 kernel-thread
//------------------------------------------------------------------------------
thread_t *thread_create(void *fn, void *arg) {
  // 1) 分配 PCB
  thread_t *t = (thread_t*)kmalloc(sizeof(thread_t));
  if (!t) return NULL;
  memset_((char*)&t->context, 0, sizeof(thread_context_t));

  // 2) 分配 kernel stack
  t->sp = kmalloc(THREAD_STK_SIZE);
  if (!t->sp) {
      kfree(t);
      return NULL;
  }
  
  // 3) 初始化 context，讓 switch_to 後跳到 fn(arg)
  uint64_t stack_top = (uint64_t)t->sp + THREAD_STK_SIZE;
  t->context.sp = stack_top;   // SP
  t->context.fp = stack_top;   // FP
  t->context.lr = (uint64_t)fn; // LR ← fn

  // 4) 其他欄位
  t->pid        = next_pid++;
  t->status     = THREAD_READY;
  t->trapframe  = NULL;        // kernel-thread 不用 trapframe
  t->pagetable  = NULL;        // 未用 MMU 時保留 NULL

  // 5) 插入 run_queue
  lock();
  if (!run_queue) {
      run_queue = t->next = t->prev = t;
  } else {
      thread_t *tail = run_queue->prev;
      tail->next = t;
      t->prev     = tail;
      t->next     = run_queue;
      run_queue->prev = t;
  }
  num_thread++;
  unlock();

  return t;
}

//------------------------------------------------------------------------------
// schedule: Round-robin
//------------------------------------------------------------------------------
void schedule(void) {
  if (!cur_thread || !run_queue) return;
  
  lock();
  thread_t *prev = cur_thread;
  thread_t *next = prev->next;

  // 找下一個非 ZOMBIE
  while (next != prev && next->status == THREAD_ZOMBIE)
      next = next->next;
  if (next->status == THREAD_ZOMBIE) {
      unlock();
      return;
  }
  uart_puts("[schedule] cur_thread id: ");
  uart_putint(cur_thread->pid);NL;
  cur_thread = next;
  cur_thread->status = THREAD_RUNNING;
  
  // 切換 context (含 kernel SP/PC)
  uart_puts("[schedule] next->context.lr): ");
  uart_put_hex(next->context.lr);
  uart_puts("  next_mem: ");
  uart_put_hex(next);NL;
  // cur_thread->context.lr += 80;
  switch_to(&prev->context, &next->context);
  unlock();
}

//------------------------------------------------------------------------------
// kill_zombies: 回收所有 ZOMBIE thread
//------------------------------------------------------------------------------
void kill_zombies(void) {
  if (!run_queue) return;

  thread_t *t = run_queue;
  do {
      thread_t *n = t->next;
      if (t->status == THREAD_ZOMBIE) {
          // 拔掉 run_queue 裡的節點
          if (t == run_queue) run_queue = n;
          t->prev->next = t->next;
          t->next->prev = t->prev;

          uart_puts("kill pid: ");
          uart_putint(t->pid);NL;
          // 釋放資源
          kfree(t->sp);
          kfree(t);
          num_thread--;
      }
      t = n;
  } while (t != run_queue);
}

//------------------------------------------------------------------------------
// idle_task: 只回收zombie並讓出 CPU
//------------------------------------------------------------------------------
void idle_task(void) {
  while (1) {
      kill_zombies();
      schedule();
  }
}


//------------------------------------------------------------------------------
// foo: 範例 kernel-thread
//------------------------------------------------------------------------------
void foo(void) {
  for (int i = 0; i < 10; i++) {
      uart_puts("Thread "); uart_putint(cur_thread->pid);
      uart_puts(" iter "); uart_putint(i); uart_puts("\n");
      delay(1000000);
      schedule();
  }
  // 做完就標為 ZOMBIE
  cur_thread->status = THREAD_ZOMBIE;
  schedule();
}

void init_user_prog(void)
{       thread_t* cur_t = get_current();
        struct cpio_file file = parse_or_find_file_in_archive("syscall.img", 1);
        if (file.size == 0) {
            uart_puts("Error: File not found in initramfs.\n");
            return;
        }
        uart_puts("found file: ");
        uart_puts(file.filename);NL;
        uart_putint(file.size);NL;
        // char* user_program_addr = simple_malloc(64);
        char* user_program_addr = kmalloc(file.size+8);
        kfree(cur_t->sp);
        // 分配記憶體來放 user program
        void* user_space = (void*) user_program_addr;
        memcpy_(user_space, file.data, file.size);
        uint64_t USER_STACK = (uint64_t)(uintptr_t)kmalloc(10000);
        cur_t->sp = USER_STACK;
        unsigned long spsr_el1 = 0x3c0;
	      asm volatile ("msr spsr_el1, %0" ::"r"(spsr_el1));
        asm volatile("msr elr_el1,%0" ::"r"(user_space));
        asm volatile("msr sp_el0,%0" :: "r"(USER_STACK+10000));
        asm volatile("eret");
}

//------------------------------------------------------------------------------
// process_create: 建 user-process，設定 trapframe + eret entry
//------------------------------------------------------------------------------
thread_t *process_create(uint64_t entry_point, uint64_t user_sp) {
  // 1) 先當成 kernel-thread 建
  thread_t *t = thread_create(NULL, NULL);

  // 2) 在 kernel stack 最頂端放 trap_frame
  uint64_t ksp = t->context.sp;
  t->trapframe = (trap_frame_t*)(ksp - sizeof(trap_frame_t));
  memset_((void*)t->trapframe, 0, sizeof(*t->trapframe));

  // 3) 初始化 EL0 狀態
  t->trapframe->elr_el1  = entry_point; // user PC
  t->trapframe->sp_el0   = user_sp;     // user SP
  t->trapframe->spsr_el1 = 0;           // PSTATE: EL0, IRQ/FIQ enabled
  // x0–x7, x8(..syscall#) 皆為 0

  // 4) 改寫 thread context，讓它從 eret_to_user 進 EL0
  t->context.sp = (uint64_t)t->trapframe;
  t->context.lr = (uint64_t)entry_point;

  // 5) 插入 run_queue
  lock();
  if (!run_queue) {
      run_queue = t->next = t->prev = t;
  } else {
      thread_t *tail = run_queue->prev;
      tail->next = t;
      t->prev     = tail;
      t->next     = run_queue;
      run_queue->prev = t;
  }
  num_thread++;
  unlock();

  // uart_puts("[creat start]\n");
  // uart_puts("pid: ");
  // uart_putint(t->pid);NL;
  // uart_puts("sp: ");
  // uart_putint(t->sp);NL;
  // uart_puts("context->sp: ");
  // uart_putint(t->context.sp);NL;
  // uart_puts("[creat end]\n");
  return t;
}
/*
void fork_test(void){
  uart_puts("\nFork Test, pid: ");
  uart_putint(call_get_pid());
  NL;
  
  int cnt = 1;
  int ret = 0;
  void *stack;
  stack = kmalloc(THREAD_STK_SIZE);

  if ((ret = call_sys_clone(0, 0, (uint64_t)stack)) == 0) { // child
      long long cur_sp;
      asm volatile("mov %0, sp" : "=r"(cur_sp));
      gdb();
      uart_puts("first child pid: ");
      uart_putint(call_get_pid());
      uart_puts(", cnt: ");
      uart_putint(cnt);
      uart_puts(", ptr: ");
      uart_b2x_64((unsigned long long)&cnt);
      uart_puts(", sp : ");
      uart_b2x_64(cur_sp);
      NL;
      ++cnt;
      stack = kmalloc(THREAD_STK_SIZE);

      if ((ret = sys_fork() ) != 0){
          asm volatile("mov %0, sp" : "=r"(cur_sp));
          
          uart_puts("first(2) child pid: ");
          uart_itoa(call_get_pid());
          uart_puts(", cnt: ");
          uart_itoa(cnt);
          uart_puts(", ptr: ");
          uart_b2x_64((unsigned long long)&cnt);
          uart_puts(", sp : ");
          uart_b2x_64(cur_sp);
          uart_putc('\n');
      }
      else{
          while (cnt < 5) {
              asm volatile("mov %0, sp" : "=r"(cur_sp));
              
              uart_puts("second child pid: ");
              uart_itoa(call_get_pid());
              uart_puts(", cnt: ");
              uart_itoa(cnt);
              uart_puts(", ptr: ");
              uart_b2x_64((unsigned long long)&cnt);
              uart_puts(", sp : ");
              uart_b2x_64(cur_sp);
              uart_putc('\n');

              delay(1000000);
              ++cnt;
          }
      }
      call_exit();
  }
  else {
      gdb();
      uart_puts("parent here, pid ");
      uart_itoa(call_get_pid());
      uart_puts(", child ");
      uart_itoa(ret);
      uart_putc('\n');
      call_exit();
  }
}
*/

