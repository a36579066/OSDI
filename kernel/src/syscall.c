#include <syscall.h>
#include <uart.h>
#include <thread.h>
#include <cpio_.h>
#include <allocator.h>
#include <mailbox.h>
#include <stdint.h>
#include <utils.h>

int test_syscall(void) {
  return 0;
}

int getpid(void) {
  thread_t *t = (thread_t *)get_current();
  return t ? t->pid : -1;
}

size_t sys_uart_write(const char *buf, size_t size) {
  if (!buf)
    return 0;
  for (size_t i = 0; i < size; i++)
    uart_putc((unsigned char)buf[i]);
  return size;
}

size_t sys_uart_read(char *buf, size_t size) {
  if (!buf || size == 0)
    return 0;

  size_t i = 0;
    while (i < size) {
        // The user shell uses polling syscalls.  Briefly allow timer IRQs while
        // waiting so another user thread can run when this one is idle in read.
        while (!(*(volatile unsigned int *)AUX_MU_LSR_REG & 0x01U)) {
      /* This is an intentional temporary enable, not a critical-section
       * release.  Keep the handler protected before and after the poll. */
      irq_enable();
      asm volatile("nop" ::: "memory");
      irq_disable();
    }

    char c = (char)(*(volatile unsigned int *)AUX_MU_IO_REG & 0xffU);
    buf[i++] = c;
    if (c == '\r' || c == '\n')
      break;
  }
  irq_disable();
  return i;
}

int sys_exec(trap_frame_t *tf, const char *name, char *const argv[]) {
  (void)argv;
  if (!tf || !name)
    return -1;

  struct cpio_file file = parse_or_find_file_in_archive(name, 1);
  if (!file.data || file.size <= 0)
    return -1;

  char *program = (char *)kmalloc((size_t)file.size);
  if (!program)
    return -1;
  memcpy_(program, file.data, (size_t)file.size);

  thread_t *current = (thread_t *)get_current();
  if (!current) {
    kfree(program);
    return -1;
  }

  // Do not free the old image: a forked sibling may still be executing it.
  // The small leak is safer than invalidating another process's text in this
  // MMU-less lab.
  current->data = program;
  current->data_size = file.size;
  current->trapframe = tf;
  tf->elr_el1 = (uint64_t)(uintptr_t)program;
  return 0;
}

static uintptr_t relocate_user_pointer(uintptr_t pointer,
                                       uintptr_t old_base,
                                       uintptr_t old_size,
                                       uintptr_t new_base,
                                       uintptr_t new_size) {
  if (pointer >= old_base && pointer <= old_base + old_size)
    return new_base + (pointer - old_base);
  return new_base + new_size;
}

int sys_fork(trap_frame_t *tf) {
  thread_t *parent = (thread_t *)get_current();
  if (!parent || !tf || !parent->is_user)
    return -1;

  thread_t *child = thread_create(NULL, NULL);
  if (!child)
    return -1;

  // The child is not considered schedulable as a user process until its
  // complete frame and stack have been prepared.
  child->is_user = 0;
  child->data = parent->data;       // shared read-only flat image
  child->data_size = parent->data_size;
  child->user_stack_size = parent->user_stack_size
                               ? parent->user_stack_size
                               : USER_STACK_SIZE;
  child->user_stack = (char *)kmalloc(child->user_stack_size);
  if (!child->user_stack) {
    child->status = THREAD_ZOMBIE;
    return -1;
  }

  uintptr_t old_stack_base = (uintptr_t)parent->user_stack;
  uintptr_t old_stack_size = parent->user_stack_size;
  if (!old_stack_base || old_stack_size == 0) {
    old_stack_size = USER_STACK_SIZE;
    old_stack_base = (uintptr_t)tf->sp_el0 - old_stack_size;
  }
  memcpy_(child->user_stack, (void *)old_stack_base, old_stack_size);

  uintptr_t child_stack_base = (uintptr_t)child->user_stack;
  uintptr_t child_stack_size = child->user_stack_size;
  uintptr_t child_stack_top = child_stack_base + child_stack_size;
  child->trapframe = (trap_frame_t *)((uintptr_t)child->sp +
                                      THREAD_STK_SIZE -
                                      EXCEPTION_FRAME_SIZE);
  memcpy_(child->trapframe, tf, sizeof(*tf));
  child->trapframe->x0 = 0;         // fork() returns 0 in the child
  child->trapframe->sp_el0 = relocate_user_pointer(
      (uintptr_t)tf->sp_el0, old_stack_base, old_stack_size,
      child_stack_base, child_stack_size);
  child->trapframe->fp = relocate_user_pointer(
      (uintptr_t)tf->fp, old_stack_base, old_stack_size,
      child_stack_base, child_stack_size);
  if (child->trapframe->sp_el0 > child_stack_top)
    child->trapframe->sp_el0 = child_stack_top;
  if (child->trapframe->fp > child_stack_top)
    child->trapframe->fp = child_stack_top;
  child->context.sp = (uint64_t)child->trapframe;
  child->context.lr = (uint64_t)eret_to_user;
  child->is_user = 1;

  for (int i = 0; i < THREAD_SIGNAL_COUNT; i++)
    child->signal_handlers[i] = parent->signal_handlers[i];

  parent->trapframe = tf;
  return child->pid;
}

int fork(trap_frame_t *tf) {
  return sys_fork(tf);
}

int exec(const char *name, char *const argv[]) {
  thread_t *current = (thread_t *)get_current();
  return current ? sys_exec(current->trapframe, name, argv) : -1;
}

int sys_mbox_call(unsigned char ch, unsigned int *mbox) {
  if (!mbox)
    return 0;
  unsigned int words = mbox[0] / sizeof(unsigned int);
  if (words < 2)
    return 0;
  return mailbox_call(ch, (volatile unsigned int *)mbox, words);
}

static thread_t *find_thread_by_pid(int pid) {
  if (!run_queue)
    return NULL;
  thread_t *t = run_queue;
  do {
    if (t->pid == pid)
      return t;
    t = t->next;
  } while (t != run_queue);
  return NULL;
}

int sys_kill(int pid) {
  thread_t *target = find_thread_by_pid(pid);
  if (!target || target == cur_thread)
    return -1;
  target->status = THREAD_ZOMBIE;
  return 0;
}

int sys_signal(int signal, uint64_t handler) {
  thread_t *current = (thread_t *)get_current();
  if (!current || signal < 0 || signal >= THREAD_SIGNAL_COUNT)
    return -1;
  current->signal_handlers[signal] = handler;
  return 0;
}

int sys_signal_kill(int pid, int signal) {
  thread_t *target = find_thread_by_pid(pid);
  if (!target || target == cur_thread || signal < 0 ||
      signal >= THREAD_SIGNAL_COUNT)
    return -1;

  // SIGKILL is the only asynchronous signal needed by the basic Lab 5
  // program.  Other signals are accepted but deferred until a full signal
  // frame implementation is added.
  if (signal == 9)
    target->status = THREAD_ZOMBIE;
  return 0;
}
