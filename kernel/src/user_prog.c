#include <user_prog.h>
#include <allocator.h>
#include <cpio_.h>
#include <uart.h>
#include <stdint.h>
#include <utils.h>
#include <exception_.h>
#include <vm.h>
#include <syscall.h>

int ready_run_user = 0;

void leave_user_mode(void) {
  if (!ready_run_user)
    return;

  ready_run_user = 0;
  uart_enable_interrupt();
}

/*
 * Lab 5's user program reads CNTPCT_EL0/CNTFRQ_EL0 for its delay loop.
 * EL0 access to the physical counter is controlled by CNTKCTL_EL1[0]
 * (EL0PCTEN). Without this bit, the first user-mode counter read traps
 * repeatedly and a framebuffer animation appears to be frozen.
 */
static void enable_user_counter_access(void) {
  uint64_t cntkctl;
  asm volatile("mrs %0, cntkctl_el1" : "=r"(cntkctl));
  cntkctl |= 1ULL;  /* EL0PCTEN: permit EL0 to read CNTPCT_EL0 */
  asm volatile("msr cntkctl_el1, %0" :: "r"(cntkctl) : "memory");
  asm volatile("isb" ::: "memory");
}

void *load_user_program(const char *filename) {
  struct cpio_file file = parse_or_find_file_in_archive(filename, 1);
  if (file.data == NULL || file.size <= 0) {
    uart_puts("Error: File not found in initramfs.\n");
    return NULL;
  }

  char *user_program = (char *)kmalloc((size_t)file.size);
  if (!user_program) {
    uart_puts("Error: Cannot allocate user program memory.\n");
    return NULL;
  }
  memcpy_(user_program, file.data, (size_t)file.size);

  uart_puts("User program loaded to RAM at ");
  uart_put_hex((uintptr_t)user_program);
  uart_puts(" (size ");
  uart_putint((unsigned long long)file.size);
  uart_puts(")\n");
  return user_program;
}

thread_t *start_user_process(const char *filename) {
  if (cur_thread == NULL)
    thread_init();

  struct cpio_file file = parse_or_find_file_in_archive(filename, 1);
  if (file.data == NULL || file.size <= 0) {
    uart_puts("Error: User program not found in initramfs.\n");
    return NULL;
  }

  if ((uint64_t)file.size > VM_USER_CODE_LIMIT) {
    uart_puts("Error: User image is larger than the 2 MB code window.\n");
    return NULL;
  }

  uint64_t image_size =
      ((uint64_t)file.size + VM_PAGE_SIZE - 1) & ~(VM_PAGE_SIZE - 1);
  void *pagetable = vm_create_user_address_space();
  user_mmap_region_t *code_region = vm_region_create(
      VM_USER_CODE_BASE, image_size,
      PROT_READ | PROT_WRITE | PROT_EXEC, 0, file.data,
      (uint64_t)file.size);
  user_mmap_region_t *stack_region = vm_region_create(
      VM_USER_STACK_BASE, USER_STACK_SIZE, PROT_READ | PROT_WRITE,
      0, NULL, 0);
  if (!pagetable || !code_region || !stack_region) {
    vm_region_list_free(code_region);
    vm_region_list_free(stack_region);
    vm_destroy_address_space(pagetable);
    uart_puts("Error: Cannot prepare user process memory.\n");
    return NULL;
  }
  code_region->next = stack_region;

  /* Do not let the timer schedule this READY thread before its VMAs exist. */
  lock();
  thread_t *process = process_create(VM_USER_CODE_BASE, VM_USER_STACK_TOP,
                                     pagetable);
  if (!process) {
    unlock();
    vm_region_list_free(code_region);
    vm_destroy_address_space(pagetable);
    uart_puts("Error: Cannot create user process.\n");
    return NULL;
  }

  /* Register code and stack as VMAs; their physical frames are allocated by
   * the first instruction fetch or stack access. */
  process->status = THREAD_BLOCKED;
  process->mmap_regions = code_region;
  process->mmap_next_addr = VM_USER_MMAP_BASE;
  process->user_stack_size = USER_STACK_SIZE;
  process->is_user = 1;
  process->status = THREAD_READY;
  unlock();

  // The user program uses polling syscalls for UART input.  Leaving the
  // kernel shell's RX/TX IRQs enabled would enqueue shell tasks forever
  // while the user program is running.
  uart_disable_interrupt();
  ready_run_user = 1;
  asm volatile("bl core_timer_enable");
  enable_user_counter_access();
  /* UART IRQs are intentionally off while user code uses polling; keep CPU
   * IRQs enabled so the preemption timer can still interrupt EL0. */
  irq_enable();

  uart_puts("Starting user process pid ");
  uart_putint((unsigned long long)process->pid);
  uart_puts("...\n");

  return process;
}

void execute_user_program(char *filename) {
  if (!start_user_process(filename))
    return;

  /* The caller is the kernel shell thread.  Save its context and let the
   * scheduler enter the new user process; do not recursively enter idle. */
  schedule();
}
