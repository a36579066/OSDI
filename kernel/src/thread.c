#include "thread.h"
#include "task_queue.h"
#include "user_prog.h"

thread_t *cur_thread = NULL;
thread_t *run_queue = NULL;
int next_pid = 0;
static int num_thread = 0;

static int thread_is_runnable(const thread_t *thread) {
  return thread != NULL &&
         (thread->status == THREAD_READY || thread->status == THREAD_RUNNING);
}

static inline void delay(int count) {
  while (count--)
    asm volatile("nop");
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
  cur_thread->status = THREAD_RUNNING;
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
  t->active_signal = THREAD_NO_ACTIVE_SIGNAL;

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
  int remaining = num_thread;
  while (next != prev && !thread_is_runnable(next) && remaining-- > 0)
    next = next->next;

  if (next == prev || !thread_is_runnable(next)) {
    if (prev->status == THREAD_RUNNING)
      prev->status = THREAD_RUNNING;
    unlock();
    return;
  }

  /* A thread that called exit has already become ZOMBIE.  Keep that state
   * so idle_task() can reclaim it after the context switch. */
  if (prev->status == THREAD_RUNNING)
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
      if (t->signal_stack)
        kfree(t->signal_stack);
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
    if (candidate != cur_thread && candidate->status == THREAD_READY) {
      found = 1;
      break;
    }
    candidate = candidate->next;
  } while (candidate != run_queue);
  unlock();
  return found;
}

int has_live_user_thread(void) {
  if (!run_queue)
    return 0;

  int found = 0;
  lock();
  thread_t *candidate = run_queue;
  do {
    if (candidate->is_user && candidate->status != THREAD_ZOMBIE) {
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

      /* The kernel shell owns UART RX whenever no user program is polling
       * it.  Deferred UART work is drained here because main() no longer
       * owns the event loop after threading starts. */
      if (!has_live_user_thread())
        leave_user_mode();
      task_execute();

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
  int remaining = num_thread;
  while (candidate && candidate != from && remaining-- > 0) {
    if (candidate->is_user && candidate->status == THREAD_READY &&
        candidate->trapframe != NULL)
      return candidate;
    candidate = candidate->next;
  }
  return NULL;
}

static thread_t *next_runnable_kernel_thread(thread_t *from) {
  if (!from || !run_queue)
    return NULL;

  thread_t *candidate = from->next;
  int remaining = num_thread;
  while (candidate && remaining-- > 0) {
    if (!candidate->is_user && thread_is_runnable(candidate))
      return candidate;
    candidate = candidate->next;
  }
  return NULL;
}

/* The exception epilogue can only finish with load_all; eret, so convert the
 * outgoing user's frame into a tiny EL1 bridge.  exception_to_kernel then
 * discards this user stack and restores the selected kernel thread's real
 * thread_context_t. */
static trap_frame_t *bridge_exception_to_kernel(trap_frame_t *tf,
                                                thread_t *next) {
  cur_thread = next;
  cur_thread->status = THREAD_RUNNING;
  asm volatile("msr tpidr_el1, %0" :: "r"(cur_thread));

  tf->spsr_el1 = 0x5;  // EL1h, with IRQ state enabled as in normal kernel code.
  tf->elr_el1 = (uint64_t)exception_to_kernel;
  return tf;
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
  if (next) {
    cur_thread = next;
    cur_thread->status = THREAD_RUNNING;
    asm volatile("msr tpidr_el1, %0" :: "r"(cur_thread));
    return cur_thread->trapframe;
  }

  thread_t *next_kernel = next_runnable_kernel_thread(cur_thread);
  if (next_kernel)
    return bridge_exception_to_kernel(tf, next_kernel);

  (void)status;
  return tf;
}

/* Lab 5 has one terminal and no background-job UI.  If its interactive user
 * shell exits after fork(), leave no framebuffer-only child owning the CPU
 * and an unreachable terminal.  A future process model with wait/job control
 * should use exit_current_thread() directly instead. */
trap_frame_t *exit_user_session(trap_frame_t *tf, int status) {
  if (!tf || !cur_thread || !cur_thread->is_user)
    return exit_current_thread(tf, status);

  thread_t *next_kernel = NULL;
  lock();
  if (run_queue) {
    thread_t *candidate = run_queue;
    int remaining = num_thread;
    while (candidate && remaining-- > 0) {
      if (candidate != cur_thread && candidate->is_user)
        candidate->status = THREAD_ZOMBIE;
      if (next_kernel == NULL && !candidate->is_user &&
          thread_is_runnable(candidate))
        next_kernel = candidate;
      candidate = candidate->next;
    }
  }
  unlock();

  /* All user threads were deliberately stopped above, so do not call the
   * normal user round-robin search.  Resume a real kernel context through
   * the exception trampoline instead of manufacturing a kernel trap frame. */
  cur_thread->trapframe = tf;
  cur_thread->status = THREAD_ZOMBIE;
  if (next_kernel != NULL)
    return bridge_exception_to_kernel(tf, next_kernel);

  return tf;
}
