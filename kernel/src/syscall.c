#include <syscall.h>
#include <uart.h>
#include <thread.h>
#include <cpio_.h>
#include <allocator.h>
#include <mailbox.h>
#include <stdint.h>
#include <utils.h>
#include <vm.h>
#include <mmu.h>

int test_syscall(void) {
  return 0;
}

int getpid(void) {
  thread_t *t = (thread_t *)get_current();
  return t ? t->pid : -1;
}

static int copy_from_user(thread_t *thread, void *dst, uint64_t src,
                          size_t size)
{
  uint8_t *out = (uint8_t *)dst;
  while (size) {
    uint64_t pa;
    if (vm_translate_user(thread->pagetable, src, &pa, 0, 0) != 0 &&
        sys_mmap_fault(thread, src, MMAP_ACCESS_READ) != 0)
      return -1;
    size_t count = (size_t)(VM_PAGE_SIZE - (src & (VM_PAGE_SIZE - 1)));
    if (count > size)
      count = size;
    if (vm_copy_from_user(thread->pagetable, out, src, count) != 0)
      return -1;
    out += count;
    src += count;
    size -= count;
  }
  return 0;
}

static int copy_to_user(thread_t *thread, uint64_t dst, const void *src,
                        size_t size)
{
  const uint8_t *in = (const uint8_t *)src;
  while (size) {
    uint64_t pa;
    if (vm_translate_user(thread->pagetable, dst, &pa, 1, 0) != 0 &&
        vm_handle_cow_fault(thread->pagetable, dst) != 0 &&
        sys_mmap_fault(thread, dst, MMAP_ACCESS_WRITE) != 0)
      return -1;
    size_t count = (size_t)(VM_PAGE_SIZE - (dst & (VM_PAGE_SIZE - 1)));
    if (count > size)
      count = size;
    if (vm_copy_to_user(thread->pagetable, dst, in, count) != 0)
      return -1;
    in += count;
    dst += count;
    size -= count;
  }
  return 0;
}

/* exec starts a fresh user program.  A signal handler belongs to the old
 * image, so its pending state, temporary stack, and registrations cannot be
 * carried into the new image. */
static void reset_signal_state_for_exec(thread_t *thread) {
  thread->pending_signals = 0;
  thread->signal_active = 0;
  thread->active_signal = THREAD_NO_ACTIVE_SIGNAL;
  memset_(&thread->saved_signal_tf, 0, sizeof(thread->saved_signal_tf));
  memset_(thread->signal_handlers, 0, sizeof(thread->signal_handlers));
}

static int copy_user_string(thread_t *thread, uint64_t user_string,
                            char *dst, size_t capacity)
{
  if (!thread || !dst || capacity == 0)
    return -1;
  for (size_t i = 0; i < capacity; i++) {
    if (copy_from_user(thread, &dst[i], user_string + i, 1) != 0)
      return -1;
    if (dst[i] == '\0')
      return 0;
  }
  dst[capacity - 1] = '\0';
  return -1;
}

size_t sys_uart_write(const char *buf, size_t size) {
  thread_t *current = (thread_t *)get_current();
  if (!buf || !current || !current->pagetable)
    return (size_t)-1;

  char chunk[64];
  size_t written = 0;
  while (written < size) {
    size_t count = size - written;
    if (count > sizeof(chunk))
      count = sizeof(chunk);
    if (copy_from_user(current, chunk,
                       (uint64_t)(uintptr_t)buf + written, count) != 0)
      return written ? written : (size_t)-1;
    for (size_t i = 0; i < count; i++)
      uart_putc((unsigned char)chunk[i]);
    written += count;
  }
  return written;
}

size_t sys_uart_read(char *buf, size_t size) {
  thread_t *current = (thread_t *)get_current();
  if (!buf || size == 0 || !current || !current->pagetable)
    return (size_t)-1;
  
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
    if (copy_to_user(current, (uint64_t)(uintptr_t)buf + i, &c, 1) != 0) {
      irq_disable();
      return i ? i : (size_t)-1;
    }
    i++;
    if (c == '\r' || c == '\n')
      break;
  }
  irq_disable();
  return i;
}

int sys_exec(trap_frame_t *tf, const char *name, char *const argv[]) {
  (void)argv;
  thread_t *current = (thread_t *)get_current();
  if (!tf || !name || !current || !current->is_user || !current->pagetable)
    return -1;

  char filename[128];
  if (copy_user_string(current, (uint64_t)(uintptr_t)name,
                       filename, sizeof(filename)) != 0)
    return -1;

  struct cpio_file file = parse_or_find_file_in_archive(filename, 1);
  if (!file.data || file.size <= 0)
    return -1;

  if ((uint64_t)file.size > VM_USER_CODE_LIMIT)
    return -1;

  uint64_t image_size = ((uint64_t)file.size + VM_PAGE_SIZE - 1) &
                        ~(VM_PAGE_SIZE - 1);
  uint64_t stack_size = current->user_stack_size
                            ? current->user_stack_size : USER_STACK_SIZE;
  void *new_pagetable = vm_create_user_address_space();
  user_mmap_region_t *code_region = vm_region_create(
      VM_USER_CODE_BASE, image_size,
      PROT_READ | PROT_WRITE | PROT_EXEC, 0, file.data,
      (uint64_t)file.size);
  user_mmap_region_t *stack_region = vm_region_create(
      VM_USER_STACK_BASE, stack_size, PROT_READ | PROT_WRITE, 0, NULL, 0);
  if (!new_pagetable || !code_region || !stack_region) {
    vm_region_list_free(code_region);
    vm_region_list_free(stack_region);
    vm_destroy_address_space(new_pagetable);
    return -1;
  }
  code_region->next = stack_region;

  void *old_pagetable = current->pagetable;
  user_mmap_region_t *old_regions = current->mmap_regions;
  current->pagetable = new_pagetable;
  current->mmap_regions = code_region;
  current->mmap_next_addr = VM_USER_MMAP_BASE;
  current->trapframe = tf;
  current->user_stack_size = USER_STACK_SIZE;
  tf->elr_el1 = VM_USER_CODE_BASE;
  tf->sp_el0 = VM_USER_STACK_TOP;
  tf->x0 = 0;

  /* Install the new mappings before releasing frames referenced by the old
   * image. EL1 continues through TTBR1 and its low kernel aliases remain. */
  mmu_switch_ttbr0((uint64_t)(uintptr_t)new_pagetable);
  reset_signal_state_for_exec(current);
  vm_region_list_free(old_regions);
  vm_destroy_address_space(old_pagetable);
  return 0;
}

int sys_fork(trap_frame_t *tf) {
  thread_t *parent = (thread_t *)get_current();
  if (!parent || !tf || !parent->is_user)
    return -1;

  /* Keep the scheduler from observing the child before its address space,
   * trap frame, and VMA list have all been cloned. */
  lock();
  thread_t *child = thread_create(NULL, NULL);
  if (!child) {
    unlock();
    return -1;
  }
  child->status = THREAD_BLOCKED;

  // The child is not considered schedulable as a user process until its
  // complete frame and stack have been prepared.
  child->is_user = 0;
  child->user_stack_size = parent->user_stack_size
                               ? parent->user_stack_size
                               : USER_STACK_SIZE;
  void *child_pagetable = vm_create_user_address_space();
  if (!child_pagetable)
    goto fork_failed;

  child->pagetable = child_pagetable;
  if (vm_clone_user_mappings_cow(parent->pagetable, child_pagetable) != 0 ||
      sys_mmap_clone(parent, child) != 0)
    goto fork_failed;

  child->pending_signals = parent->pending_signals;
  child->signal_active = parent->signal_active;
  child->active_signal = parent->active_signal;
  memcpy_(&child->saved_signal_tf, &parent->saved_signal_tf,
          sizeof(child->saved_signal_tf));

  child->trapframe = (trap_frame_t *)((uintptr_t)child->sp +
                                      THREAD_STK_SIZE -
                                      EXCEPTION_FRAME_SIZE);
  memcpy_(child->trapframe, tf, sizeof(*tf));
  child->trapframe->x0 = 0;         // fork() returns 0 in the child
  child->context.sp = (uint64_t)child->trapframe;
  child->context.lr = (uint64_t)eret_to_user;
  child->is_user = 1;

  for (int i = 0; i < THREAD_SIGNAL_COUNT; i++)
    child->signal_handlers[i] = parent->signal_handlers[i];

  parent->trapframe = tf;
  int child_pid = child->pid;
  child->status = THREAD_READY;
  unlock();
  return child_pid;

fork_failed:
  sys_mmap_release(child);
  vm_destroy_address_space(child_pagetable);
  child->pagetable = NULL;
  child->status = THREAD_ZOMBIE;
  unlock();
  return -1;
}

int fork(trap_frame_t *tf) {
  return sys_fork(tf);
}

int exec(const char *name, char *const argv[]) {
  thread_t *current = (thread_t *)get_current();
  return current ? sys_exec(current->trapframe, name, argv) : -1;
}

static int map_framebuffer_response(thread_t *thread,
                                    volatile unsigned int *message,
                                    unsigned int words)
{
  for (unsigned int i = 2; i + 4 < words; ) {
    unsigned int tag = message[i];
    unsigned int value_bytes = message[i + 1];
    if (tag == 0)
      break;
    if (tag == 0x00040001U && value_bytes >= 8) {
      uint64_t pa = message[i + 3] & 0x3FFFFFFFULL;
      uint64_t size = message[i + 4];
      if (pa == 0 || size == 0 || size > (1ULL << 30) - pa)
        return -1;
      uint64_t page_start = pa & ~(VM_PAGE_SIZE - 1);
      uint64_t page_end = (pa + size + VM_PAGE_SIZE - 1) &
                          ~(VM_PAGE_SIZE - 1);
      return vm_map_user_pages(thread->pagetable, page_start, page_start,
                               page_end - page_start,
                               VM_MAP_USER | VM_MAP_WRITE | VM_MAP_DEVICE |
                               VM_MAP_REPLACE);
    }
    i += 3 + (value_bytes + sizeof(unsigned int) - 1) /
                   sizeof(unsigned int);
  }
  return 0;
}

int sys_mbox_call(unsigned char ch, unsigned int *mbox) {
  thread_t *current = (thread_t *)get_current();
  if (!mbox || !current || !current->pagetable)
    return 0;

  unsigned int bytes;
  uint64_t user_addr = (uint64_t)(uintptr_t)mbox;
  if (copy_from_user(current, &bytes, user_addr, sizeof(bytes)) != 0 ||
      bytes < 2 * sizeof(unsigned int) || bytes > 16384 ||
      (bytes & (sizeof(unsigned int) - 1)) != 0)
    return 0;

  unsigned int words = bytes / sizeof(unsigned int);
  uint64_t alloc_size = (bytes + VM_PAGE_SIZE - 1) & ~(VM_PAGE_SIZE - 1);
  volatile unsigned int *staging =
      (volatile unsigned int *)kmalloc((size_t)alloc_size);
  if (!staging)
    return 0;

  if (copy_from_user(current, (void *)staging, user_addr, bytes) != 0) {
    kfree((void *)staging);
    return 0;
  }

  int result = mailbox_call(ch, staging, words);
  if (result && map_framebuffer_response(current, staging, words) != 0)
    result = 0;
  if (result && copy_to_user(current, user_addr,
                             (const void *)staging, bytes) != 0)
    result = 0;
  kfree((void *)staging);
  return result;
}

/* The caller holds lock(), because run_queue may be changed by scheduling or
 * zombie collection. */
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
  lock();
  thread_t *target = find_thread_by_pid(pid);
  if (!target || target == cur_thread) {
    unlock();
    return -1;
  }
  target->status = THREAD_ZOMBIE;
  unlock();
  return 0;
}

int sys_signal(int signal, uint64_t handler) {
  thread_t *current = (thread_t *)get_current();
  if (!current || signal < 0 || signal >= THREAD_SIGNAL_COUNT ||
      signal == SIGKILL)
    return -1;
  if (handler != 0) {
    uint64_t handler_pa;
    if (vm_translate_user(current->pagetable, handler, &handler_pa, 0, 1) != 0)
      return -1;
  }

  lock();
  current->signal_handlers[signal] = handler;
  unlock();
  return 0;
}

int sys_signal_kill(int pid, int signal) {
  if (signal < 0 || signal >= THREAD_SIGNAL_COUNT)
    return -1;

  /* Sending is deliberately small: record the event and let the target
   * process consume it at its next EL0 return boundary. */
  lock();
  thread_t *target = find_thread_by_pid(pid);
  if (!target || target->status == THREAD_ZOMBIE) {
    unlock();
    return -1;
  }

  target->pending_signals |= (1ULL << signal);
  unlock();
  return 0;
}

static int first_pending_signal(uint64_t pending_signals) {
  for (int signal = 0; signal < THREAD_SIGNAL_COUNT; signal++) {
    if (pending_signals & (1ULL << signal))
      return signal;
  }
  return THREAD_NO_ACTIVE_SIGNAL;
}

/* AArch64: mov x8, #NR_SIGRETURN; svc #0.  The trampoline lives at the
 * bottom of the separate handler stack, while the handler stack grows down
 * from its top. */
static void write_sigreturn_trampoline(uintptr_t address) {
  uint32_t *code = (uint32_t *)address;
  code[0] = 0xD2800000U | ((uint32_t)NR_SIGRETURN << 5) | 8U;
  code[1] = 0xD4000001U;

  /* The kernel has just written executable EL0 memory.  Make the two words
   * visible to the instruction cache before eret enters the handler. */
  asm volatile("dc cvau, %0" :: "r"(address) : "memory");
  asm volatile("dc cvau, %0" :: "r"(address + sizeof(uint32_t)) : "memory");
  asm volatile("dsb ish" ::: "memory");
  asm volatile("ic ivau, %0" :: "r"(address) : "memory");
  asm volatile("ic ivau, %0" :: "r"(address + sizeof(uint32_t)) : "memory");
  asm volatile("dsb ish\n\tisb" ::: "memory");
}

/* Replace the return frame with a normal EL0 function-call context. */
static int enter_user_signal_handler(thread_t *thread, trap_frame_t *tf,
                                     int signal) {
  uint64_t handler = thread->signal_handlers[signal];
  if (handler == 0)
    return 0;

  uint64_t handler_pa;
  if (vm_translate_user(thread->pagetable, handler, &handler_pa, 0, 1) != 0)
    return -1;

  if (vm_map_user_buffer(thread->pagetable, VM_USER_SIGNAL_STACK, NULL,
                         SIGNAL_STACK_SIZE,
                         VM_MAP_USER | VM_MAP_WRITE) != 0)
    return -1;

  if (vm_map_user_buffer(thread->pagetable, VM_USER_SIGNAL_TRAMPOLINE, NULL,
                         VM_PAGE_SIZE, VM_MAP_USER | VM_MAP_EXEC) != 0) {
    vm_unmap_user_page(thread->pagetable, VM_USER_SIGNAL_STACK);
    return -1;
  }

  uint64_t stack_pa;
  if (vm_translate_user(thread->pagetable, VM_USER_SIGNAL_TRAMPOLINE,
                        &stack_pa, 0, 1) != 0) {
    vm_unmap_user_page(thread->pagetable, VM_USER_SIGNAL_TRAMPOLINE);
    vm_unmap_user_page(thread->pagetable, VM_USER_SIGNAL_STACK);
    return -1;
  }
  write_sigreturn_trampoline(stack_pa);

  /* Keep the interrupted EL0 register set in kernel-owned memory. */
  memcpy_(&thread->saved_signal_tf, tf, sizeof(*tf));
  thread->signal_active = 1;
  thread->active_signal = signal;

  /* eret now enters handler() in EL0.  A normal `ret` in handler jumps to
   * the trampoline, which issues NR_SIGRETURN. */
  tf->elr_el1 = handler;
  tf->sp_el0 = VM_USER_SIGNAL_STACK + SIGNAL_STACK_SIZE;
  tf->lr = VM_USER_SIGNAL_TRAMPOLINE;
  tf->x0 = (uint64_t)signal;
  return 1;
}

trap_frame_t *sys_sigreturn(trap_frame_t *tf) {
  thread_t *thread = (thread_t *)get_current();
  if (!tf || !thread || !thread->is_user || !thread->signal_active) {
    if (tf)
      tf->x0 = (uint64_t)-1;
    return tf;
  }

  vm_unmap_user_page(thread->pagetable, VM_USER_SIGNAL_STACK);
  vm_unmap_user_page(thread->pagetable, VM_USER_SIGNAL_TRAMPOLINE);
  memcpy_(tf, &thread->saved_signal_tf, sizeof(*tf));
  thread->signal_active = 0;
  thread->active_signal = THREAD_NO_ACTIVE_SIGNAL;
  thread->trapframe = tf;

  return tf;
}

/*
 * This runs only after the syscall/IRQ path has selected the trap frame that
 * will actually be restored by eret.  It is therefore the safe boundary for
 * asynchronous signal delivery.
 */
trap_frame_t *prepare_user_return(trap_frame_t *tf) {
  if (!tf || !cur_thread || !cur_thread->is_user)
    return tf;

  cur_thread->trapframe = tf;

  if ((cur_thread->pending_signals & (1ULL << SIGKILL)) == 0)
    goto deliver_registered_signal;

  cur_thread->pending_signals &= ~(1ULL << SIGKILL);
  return exit_current_thread(tf, 128 + SIGKILL);

deliver_registered_signal:
  if (cur_thread->signal_active || cur_thread->pending_signals == 0)
    return tf;

  int signal = first_pending_signal(cur_thread->pending_signals);
  if (signal == THREAD_NO_ACTIVE_SIGNAL)
    return tf;

  uint64_t handler = cur_thread->signal_handlers[signal];
  if (handler == 0) {
    /* The assignment requires SIGKILL's default action.  Other default
     * actions are intentionally "ignore" until they are specified. */
    cur_thread->pending_signals &= ~(1ULL << signal);
    return tf;
  }

  int result = enter_user_signal_handler(cur_thread, tf, signal);
  if (result > 0)
    cur_thread->pending_signals &= ~(1ULL << signal);
  return tf;
}
