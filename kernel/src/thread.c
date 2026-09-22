#include "thread.h"

thread_t *cur_thread = NULL;
thread_t *run_queue = NULL;
int next_pid = 0;
static int num_thread = 0;

static inline void delay(int count) {
  while (count--)
    asm volatile("nop");
}

static trap_frame_t *kernel_frame(thread_t *t) {
  return (trap_frame_t *)((uintptr_t)t->sp + THREAD_STK_SIZE -
                          EXCEPTION_FRAME_SIZE);
}

static void init_kernel_frame(thread_t *t) {
  trap_frame_t *tf = kernel_frame(t);
  memset_(tf, 0, sizeof(*tf));
  tf->spsr_el1 = 0x5;              // EL1h
  tf->elr_el1 = (uint64_t)idle_task;
  t->trapframe = tf;
}

//------------------------------------------------------------------------------
// thread_init: 建立 idle task，並設為 current
//------------------------------------------------------------------------------
void thread_init(void) {
  if (cur_thread != NULL)
    return;

  lock();
  thread_t *idle = thread_create(idle_task, NULL);
  if (idle == NULL) {
    unlock();
    return;
  }
  cur_thread = idle;
  asm volatile("msr tpidr_el1, %0" :: "r"(idle));
  uart_puts("Thread system initialized.\n");
  unlock();
}

//------------------------------------------------------------------------------
// thread_create: 建 kernel-thread
//------------------------------------------------------------------------------
thread_t *thread_create(void *fn, void *arg) {
  thread_t *t = (thread_t *)kmalloc(sizeof(thread_t));
  if (!t)
    return NULL;

  memset_(t, 0, sizeof(*t));
  t->sp = kmalloc(THREAD_STK_SIZE);
  if (!t->sp) {
    kfree(t);
    return NULL;
  }

  uint64_t stack_top = (uint64_t)t->sp + THREAD_STK_SIZE;
  t->context.sp = stack_top;
  t->context.fp = stack_top;
  t->context.lr = (uint64_t)fn;
  t->pid = next_pid++;
  t->status = THREAD_READY;
  t->trapframe = NULL;
  t->pagetable = NULL;
  t->user_stack = NULL;
  t->user_stack_size = 0;
  t->is_user = 0;

  lock();
  if (!run_queue) {
    run_queue = t->next = t->prev = t;
  } else {
    thread_t *tail = run_queue->prev;
    tail->next = t;
    t->prev = tail;
    t->next = run_queue;
    run_queue->prev = t;
  }
  num_thread++;
  unlock();
  (void)arg;
  return t;
}

//------------------------------------------------------------------------------
// schedule: Round-robin for kernel threads
//------------------------------------------------------------------------------
void schedule(void) {
  if (!cur_thread || !run_queue)
    return;

  lock();
  thread_t *prev = cur_thread;
  thread_t *next = prev->next;
  while (next != prev && next->status == THREAD_ZOMBIE)
    next = next->next;

  if (next == prev || next->status == THREAD_ZOMBIE) {
    if (prev->status != THREAD_ZOMBIE)
      prev->status = THREAD_RUNNING;
    unlock();
    return;
  }

  /* A thread that called exit has already become ZOMBIE.  Keep that state
   * so idle_task() can reclaim it after the context switch. */
  if (prev->status != THREAD_ZOMBIE)
    prev->status = THREAD_READY;
  cur_thread = next;
  cur_thread->status = THREAD_RUNNING;
  asm volatile("msr tpidr_el1, %0" :: "r"(cur_thread));
  unlock();

  switch_to(&prev->context, &next->context);
}

//------------------------------------------------------------------------------
// kill_zombies: 回收所有非-current 的 ZOMBIE thread
//------------------------------------------------------------------------------
void kill_zombies(void) {
  if (!run_queue)
    return;

  lock();
  thread_t *t = run_queue;
  int remaining = num_thread;
  while (run_queue && t && remaining-- > 0) {
    thread_t *next = t->next;
    if (t != cur_thread && t->status == THREAD_ZOMBIE) {
      if (t->next == t) {
        run_queue = NULL;
      } else {
        t->prev->next = t->next;
        t->next->prev = t->prev;
        if (run_queue == t)
          run_queue = t->next;
      }

      if (t->user_stack)
        kfree(t->user_stack);
      if (t->sp)
        kfree(t->sp);
      kfree(t);
      num_thread--;
      if (!run_queue)
        break;
    }
    t = next;
  }
  unlock();
}

static int has_runnable_other_thread(void) {
  if (!cur_thread || !run_queue)
    return 0;

  int found = 0;
  lock();
  thread_t *candidate = run_queue;
  do {
    if (candidate != cur_thread && candidate->status != THREAD_ZOMBIE) {
      found = 1;
      break;
    }
    candidate = candidate->next;
  } while (candidate != run_queue);
  unlock();
  return found;
}

//------------------------------------------------------------------------------
// idle_task: 回收 zombie；沒有 runnable thread 時才睡眠
//------------------------------------------------------------------------------
void idle_task(void) {
  while (1) {
      kill_zombies();
      schedule();

      /* A kernel thread voluntarily switches back to idle.  Do not sleep
       * while another runnable thread is waiting for its turn. */
      if (has_runnable_other_thread())
        continue;

      irq_enable();
      asm volatile("wfi" ::: "memory");
  }
}

//------------------------------------------------------------------------------
// foo: 範例 kernel-thread
//------------------------------------------------------------------------------
void foo(void) {
  for (int i = 0; i < 10; i++) {
    uart_puts("Thread ");
    uart_putint(cur_thread->pid);
    uart_puts(" iter ");
    uart_putint(i);
    uart_puts("\n");
    delay(1000000);
    schedule();
  }
  cur_thread->status = THREAD_ZOMBIE;
  schedule();
  while (1)
    asm volatile("wfe");
}

// Kept for compatibility with the old shell path.  New user programs are
// created by start_user_process() and entered through process_create().
void init_user_prog(void) {
  while (1)
    schedule();
}

//------------------------------------------------------------------------------
// process_create: 建 user-process，設定 trapframe + eret entry
//------------------------------------------------------------------------------
thread_t *process_create(uint64_t entry_point, uint64_t user_sp) {
  thread_t *t = thread_create(NULL, NULL);
  if (!t)
    return NULL;

  uint64_t ksp = (uint64_t)t->sp + THREAD_STK_SIZE;
  t->trapframe = (trap_frame_t *)(ksp - EXCEPTION_FRAME_SIZE);
  memset_(t->trapframe, 0, sizeof(*t->trapframe));
  t->trapframe->elr_el1 = entry_point;
  t->trapframe->sp_el0 = user_sp;
  t->trapframe->spsr_el1 = 0;       // EL0t, IRQ/FIQ enabled
  t->is_user = 1;

  // switch_to() loads this context and returns into eret_to_user(), which
  // restores the complete frame and enters EL0.
  t->context.sp = (uint64_t)t->trapframe;
  t->context.lr = (uint64_t)eret_to_user;
  return t;
}

static thread_t *next_user_thread(thread_t *from) {
  if (!from || !run_queue)
    return NULL;

  thread_t *candidate = from->next;
  while (candidate != from) {
    if (candidate->is_user && candidate->status != THREAD_ZOMBIE &&
        candidate->trapframe != NULL)
      return candidate;
    candidate = candidate->next;
  }
  return NULL;
}

trap_frame_t *schedule_from_irq(trap_frame_t *tf) {
  if (!tf || !cur_thread || !run_queue)
    return tf;

  // The frame lives on this thread's kernel stack.  It remains valid while
  // another user thread is running and becomes the resume point later.
  cur_thread->trapframe = tf;
  if (!cur_thread->is_user)
    return tf;

  thread_t *next = next_user_thread(cur_thread);
  if (!next)
    return tf;

  cur_thread->status = THREAD_READY;
  cur_thread = next;
  cur_thread->status = THREAD_RUNNING;
  asm volatile("msr tpidr_el1, %0" :: "r"(cur_thread));
  return cur_thread->trapframe;
}

trap_frame_t *exit_current_thread(trap_frame_t *tf, int status) {
  if (!tf || !cur_thread)
    return tf;

  cur_thread->trapframe = tf;
  cur_thread->status = THREAD_ZOMBIE;

  thread_t *next = next_user_thread(cur_thread);
  if (!next) {
    thread_t *candidate = cur_thread->next;
    while (candidate != cur_thread && candidate->status == THREAD_ZOMBIE)
      candidate = candidate->next;
    if (candidate != cur_thread) {
      if (candidate->trapframe == NULL)
        init_kernel_frame(candidate);
      next = candidate;
    }
  }

  if (next) {
    cur_thread = next;
    cur_thread->status = THREAD_RUNNING;
    asm volatile("msr tpidr_el1, %0" :: "r"(cur_thread));
    return cur_thread->trapframe;
  }

  (void)status;
  return tf;
}
