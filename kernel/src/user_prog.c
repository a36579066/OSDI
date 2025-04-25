#include <user_prog.h>
#include <allocator.h>
#include <cpio_.h>
#include <uart.h>
#include <stdint.h>
#include <utils.h>
#include <exception_.h>

int ready_run_user = 0;

extern void user_program(void);

/*
0x3c0 = 0b 0011 1100 0000
          |||| |||| ||||
          |||| |||| |||└─ M[3:0] = 0000 = EL0t
          |||| |||| ||└── 0
          |||| |||| |└─── 0
          |||| |||| └──── 0
          |||| |||└────── res[0] and M[4] 
          |||| ||└─────── I = 1  <--- 關閉 IRQ
          |||| |└──────── F = 1
          |||| └───────── A = 1
          |||──────────── D = 1
*/
void switch_to_user_mode(void *func, void* sp_addr) {
  ready_run_user = 1;
  
  asm volatile("bl core_timer_enable");
  enable_interrupts();
  *(unsigned int*)CORE0_TIMER_IRQ_CTRL = 2;

  // uint64_t freq;
  // asm volatile ("mrs %0, cntfrq_el0" : "=r" (freq));
  // uint64_t tval = 2 * freq;
  // asm volatile ("msr cntp_tval_el0, %0" :: "r" (tval));
  // *(unsigned int*)CORE0_TIMER_IRQ_CTRL = 2;

  unsigned long spsr_el1 = 0x3c0;
	asm volatile ("msr spsr_el1, %0" ::"r"(spsr_el1));

	unsigned long jump_addr = (uintptr_t)func;
	asm volatile("msr elr_el1,%0" ::"r"(jump_addr));
	unsigned long sp = (unsigned long) sp_addr;
	asm volatile("msr sp_el0,%0" :: "r"(sp));
  // asm volatile("mov x0, #0");          // `0b0000` -> M=0（EL0），DAIF=0（允許 IRQ）
  // asm volatile("msr SPSR_EL1, x0");
  // spsr_el1 = 0x0; // M = 0 (EL0), DAIF = 0b0000 (允許 IRQ/FIQ)
  // asm volatile("msr SPSR_EL1, %0" :: "r"(spsr_el1));
  // *(unsigned int*)CORE0_TIMER_IRQ_CTRL = 2;
	asm volatile("eret");

  uart_puts("After eret (should never reach here)!\n");
}


void* load_user_program(const char* filename) {
    struct cpio_file file = parse_or_find_file_in_archive(filename, 1);
    if (file.size == 0) {
        uart_puts("Error: File not found in initramfs.\n");
        return 0;
    }
    // char* user_program_addr = simple_malloc(64);
    char* user_program_addr = kmalloc(64);
    
    // 分配記憶體來放 user program
    void* user_space = (void*) user_program_addr;
    memcpy_(user_space, file.data, file.size);

    uart_puts("User program loaded to RAM at ");
    uart_put_hex((uintptr_t)user_space);
    uart_puts("\n");

    return user_space;
}


void execute_user_program(char *filename) {
  void *prog_start = load_user_program(filename);
  if (prog_start == 0) {
      uart_puts("Error: Failed to load program.\n");
      return;
  }
  // void* ptr = simple_malloc(4096);
  void* ptr = kmalloc(4096);
  uart_puts("SP_EL0 = ");
  uart_put_hex((uintptr_t)ptr);
  uart_puts("\n");
  uart_puts("Switching to user mode...\n");
  switch_to_user_mode(prog_start, ptr+4096);
}

// void cpio_exec_program(char* filename) {
// 	int targetfile_num = findfile(filename);

// 	char* target = (char*) 0x20000;
// 	for(unsigned i = 0; i<f[targetfile_num].file_size; i++){
// 		*target++ = f[targetfile_num].file_content_head[i];
// 	}

// 	unsigned long spsr_el1 = 0x3c0;
// 	asm volatile ("msr spsr_el1, %0" ::"r"(spsr_el1));
// 	unsigned long jump_addr = 0x20000;
// 	asm volatile("msr elr_el1,%0" ::"r"(jump_addr));
// 	unsigned long sp = (unsigned long) simple_malloc(4096);
// 	asm volatile("msr sp_el0,%0" :: "r"(sp));
// 	asm volatile("eret");
// }