.global _start
.global dtb_addr

.section ".text.boot"
#define CORE0_TIMER_IRQ_CTRL 0x40000040

_start:

    ldr x11, =dtb_addr  // get dtb_addr address
    str x0, [x11]
    // str x19, [x11] 如果要用bootloader.img就開這個上面註解掉
    // ldr x10, =dtb_addr      
    // str x19, [x10]           

    // Read CPU ID
    mrs x1, mpidr_el1       // Get CPU ID
    and x1, x1, #0xFF       // Extract the lower 8 bits of the CPU ID

    // Only allow Core 0 to execute, other cores enter wfe sleep
    cbz x1, primary_core     // If Core 0, jump to primary_core

halt:
    wfe                      // Put other cores into wait state
    b halt                   // Infinite loop to prevent execution of other programs

primary_core:
    // Set Stack Pointer
    bl from_el2_to_el1
    // msr DAIFSet, #0xf // EL1 禁止 interrupt（防止 EL1 自己處理中斷）
    ldr x1, =_stack_top;      // Load the address of the stack top
    mov sp, x1               // Set the stack pointer
    set_exception_vector_table:
        adr x0, exception_vector_table
        msr vbar_el1, x0
    
    // Initialize BSS section (zero out)
    ldr x2, =_bss_start      // Get the start address of BSS section
    ldr x3, =_bss_end        // Get the end address of BSS section
    bss_zero_loop:
        cmp x2, x3           // Check if reached the end of BSS section
        bge bss_done         // If done, jump out
        str wzr, [x2], #8    // Set current position to 0, move forward by 8 bytes
        b bss_zero_loop      // Repeat the process




bss_done:
    
    // Jump to main()
    bl main           // Call the main function in C

    // Infinite loop to prevent main() from returning
    b .
    
from_el2_to_el1:
    mov x0, (1 << 31) // EL1 uses aarch64
    msr hcr_el2, x0
    mov x0, 0x345 // EL1h (SPSel = 1) with interrupt disabled
    msr spsr_el2, x0
    msr elr_el2, lr
    eret // return to EL1



.macro save_all
    sub sp, sp, 32 * 9
    stp x0, x1, [sp ,16 * 0]
    stp x2, x3, [sp ,16 * 1]
    stp x4, x5, [sp ,16 * 2]
    stp x6, x7, [sp ,16 * 3]
    stp x8, x9, [sp ,16 * 4]
    stp x10, x11, [sp ,16 * 5]
    stp x12, x13, [sp ,16 * 6]
    stp x14, x15, [sp ,16 * 7]
    stp x16, x17, [sp ,16 * 8]
    stp x18, x19, [sp ,16 * 9]
    stp x20, x21, [sp ,16 * 10]
    stp x22, x23, [sp ,16 * 11]
    stp x24, x25, [sp ,16 * 12]
    stp x26, x27, [sp ,16 * 13]
    stp x28, x29, [sp ,16 * 14]
    str x30, [sp, 16 * 15]
    mrs x0, spsr_el1
    mrs x1, elr_el1
    stp x0, x1, [sp, 16 * 16]
    ldp x0, x1, [sp, 16 * 0] 
.endm

// load general registers from stack
.macro load_all
    ldp x0, x1, [sp ,16 * 0]
    ldp x2, x3, [sp ,16 * 1]
    ldp x4, x5, [sp ,16 * 2]
    ldp x6, x7, [sp ,16 * 3]
    ldp x8, x9, [sp ,16 * 4]
    ldp x10, x11, [sp ,16 * 5]
    ldp x12, x13, [sp ,16 * 6]
    ldp x14, x15, [sp ,16 * 7]
    ldp x16, x17, [sp ,16 * 8]
    ldp x18, x19, [sp ,16 * 9]
    ldp x20, x21, [sp ,16 * 10]
    ldp x22, x23, [sp ,16 * 11]
    ldp x24, x25, [sp ,16 * 12]
    ldp x26, x27, [sp ,16 * 13]
    ldp x28, x29, [sp ,16 * 14]
    ldr x30, [sp, 16 * 15]
    ldp x0, x1, [sp, 16 * 16]
    msr spsr_el1, x0
    msr elr_el1, x1
    ldp x0, x1, [sp ,16 * 0]
    add sp, sp, 32 * 9
.endm

exception_handler:
    save_all
    bl exception_entry
    load_all
    eret

syscall_handler:
  save_all
  mov x0, sp              // get trap frame(which is the same thing we just saved, meaning that we might modified user space registers)
  bl c_system_call_handler
  mov x1, sp
  str x0, [x1, 0]         // save return value to trap frame
  load_all
  eret

.global core_timer_enable
core_timer_enable:
  mov x0, 1
  msr cntp_ctl_el0, x0 // enable
  mrs x0, cntfrq_el0
  msr cntp_tval_el0, x0 // set expired time



  mov x0, 2
  ldr x1, =CORE0_TIMER_IRQ_CTRL
  str w0, [x1] // unmask timer interrupt
  ret
  

core_timer_handler:
  save_all
  bl core_timer_handler_function
  load_all
  eret
  // mrs x0, cntfrq_el0
  // msr cntp_tval_el0, x0

irq_handler:
  save_all
  bl irq_handler_function
  load_all
  eret

.align 11 // vector table should be aligned to 0x800
.global exception_vector_table
exception_vector_table:
  b exception_handler // branch to a handler function.
  .align 7 // entry size is 0x80, .align will pad 0
  b exception_handler
  .align 7
  b exception_handler
  .align 7
  b exception_handler
  .align 7

  b exception_handler
  .align 7
  b irq_handler
  .align 7
  b exception_handler
  .align 7
  b exception_handler
  .align 7

  b syscall_handler
  .align 7
  b irq_handler
  .align 7
  b exception_handler
  .align 7
  b exception_handler
  .align 7

  b exception_handler
  .align 7
  b exception_handler
  .align 7
  b exception_handler
  .align 7
  b exception_handler
  .align 7

.global user_program

user_program:
    mov x0, 0
1:
    add x0, x0, 1
    svc 0
    cmp x0, 5
    blt 1b
1:
    b 1b

.global get_dtb_addr
get_dtb_addr:
    ldr x0, =dtb_addr  // get dtb_addr address
    ldr x0, [x0]       // laod dtb_addr value
    ret   

.section .data
dtb_addr: .quad 0  