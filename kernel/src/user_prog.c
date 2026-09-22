#include <user_prog.h>
#include <allocator.h>
#include <cpio_.h>
#include <uart.h>
#include <stdint.h>
#include <utils.h>
#include <exception_.h>

int ready_run_user = 0;

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

  // The program is a flat, position-independent image.  Keep it in a
  // separate allocation for the lifetime of the process.
  char *program = (char *)kmalloc((size_t)file.size);
  char *user_stack = (char *)kmalloc(USER_STACK_SIZE);
  if (!program || !user_stack) {
    if (program)
      kfree(program);
    if (user_stack)
      kfree(user_stack);
    uart_puts("Error: Cannot allocate user process memory.\n");
    return NULL;
  }
  memcpy_(program, file.data, (size_t)file.size);

  uintptr_t user_stack_top = ((uintptr_t)user_stack + USER_STACK_SIZE) &
                             ~(uintptr_t)0xF;
  thread_t *process = process_create((uint64_t)(uintptr_t)program,
                                     (uint64_t)user_stack_top);
  if (!process) {
    kfree(program);
    kfree(user_stack);
    uart_puts("Error: Cannot create user process.\n");
    return NULL;
  }

  process->data = program;
  process->data_size = file.size;
  process->user_stack = user_stack;
  process->user_stack_size = USER_STACK_SIZE;

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

  // This is called by the kernel shell.  Transfer control to the scheduler;
  // the current shell task will not return until the user process exits.
  idle_task();
}
