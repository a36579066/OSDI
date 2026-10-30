.global _start
.global dtb_addr

.section ".text.boot"
.equ KERNEL_VA_BASE, 0xffff000000000000
#define CORE0_TIMER_IRQ_CTRL (KERNEL_VA_BASE + 0x40000040)
#define BOOT_PGD_PA 0x1000
#define BOOT_PUD_PA 0x2000

_start:

    ldr x11, =dtb_addr  // get dtb_addr address
    ldr x12, =KERNEL_VA_BASE
    sub x11, x11, x12   // dtb_addr is linked high but not mapped yet
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
    ldr x2, =KERNEL_VA_BASE
    sub x1, x1, x2             // use its physical address before MMU is on
    mov sp, x1               // Set the stack pointer
    
    // Initialize BSS section (zero out)
    ldr x2, =_bss_start      // Get the start address of BSS section
    ldr x3, =_bss_end        // Get the end address of BSS section
    ldr x4, =KERNEL_VA_BASE
    sub x2, x2, x4
    sub x3, x3, x4
    bss_zero_loop:
        cmp x2, x3           // Check if reached the end of BSS section
        bge bss_done         // If done, jump out
        str wzr, [x2], #8    // Set current position to 0, move forward by 8 bytes
        b bss_zero_loop      // Repeat the process




bss_done:

    bl setup_boot_translation

    /* Both TTBR0 and TTBR1 now describe this physical range.  Continue at
     * the high alias of this stub, make SP high too, then enter C. */
    adr x0, boot_high_entry
    ldr x1, =KERNEL_VA_BASE
    add x0, x0, x1
    add sp, sp, x1
    br x0

    // Infinite loop to prevent main() from returning
    b .
    
from_el2_to_el1:
    mov x0, (1 << 31) // EL1 uses aarch64
    msr hcr_el2, x0
    mov x0, 0x345 // EL1h (SPSel = 1) with interrupt disabled
    msr spsr_el2, x0
    msr elr_el2, lr
    eret // return to EL1

/* Build the temporary EL1-only identity mapping used while enabling the MMU
 * and branching from the low boot stub into the high-half kernel. */
setup_boot_translation:
    mov x0, #BOOT_PGD_PA
    mov x1, #BOOT_PUD_PA
    mov x2, #0x1000
1:
    stp xzr, xzr, [x0], #16 // clear PGD and PUD
    stp xzr, xzr, [x1], #16 //
    subs x2, x2, #16        //
    b.ne 1b                 //

    mov x0, #BOOT_PGD_PA
    mov x1, #BOOT_PUD_PA
    orr x2, x1, #0x3          // PGD[0] -> PUD table descriptor
    str x2, [x0]

    mov x2, #0x401            // AF | kernel-only | AttrIndx(0: Device) | block
    str x2, [x1]              // 0x00000000 - 0x3fffffff
    ldr x3, =0x40000000
    orr x3, x3, x2
    str x3, [x1, #8]          // 0x40000000 - 0x7fffffff

    dsb ishst
    ldr x2, =0x4400           // MAIR Attr0=Device, Attr1=Normal non-cacheable
    msr mair_el1, x2
    ldr x2, =0x80100010       // 48-bit VA, 4 KB granule for TTBR0/TTBR1
    msr tcr_el1, x2
    isb
    msr ttbr0_el1, x0
    msr ttbr1_el1, x0
    dsb ish
    isb
    mrs x2, sctlr_el1
    orr x2, x2, #1            // SCTLR_EL1.M: enable stage-1 MMU
    msr sctlr_el1, x2         // Desciption in page 2029
    isb
    ret

/* This label is linked at its low physical address but is entered through
 * TTBR1 at KERNEL_VA_BASE + physical address. */
boot_high_entry:
    ldr x0, =exception_vector_table
    msr vbar_el1, x0
    isb
    ldr x0, =main
    br x0

/* Everything below is ordinary high-half kernel text. */
.section .text



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
    // Keep the saved frame layout identical to trap_frame_t:
    // x0..x28, fp, lr, spsr_el1, elr_el1, sp_el0.
    mrs x0, spsr_el1
    mrs x1, elr_el1
    mrs x2, sp_el0
    str x0, [sp, 248]
    str x1, [sp, 256]
    str x2, [sp, 264]
    // ldp x0, x1, [sp, 16 * 0] 
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
    ldr x0, [sp, 248]
    msr spsr_el1, x0
    ldr x0, [sp, 256]
    msr elr_el1, x0
    ldr x0, [sp, 264]
    msr sp_el0, x0
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
  mov x0, sp              // pass the saved trap frame and inspect ESR_EL1
  bl lower_el_sync_handler
  bl prepare_user_return  // may replace tf when a signal terminates a thread
  mov sp, x0              // the syscall may switch to another user thread
  load_all
  eret

.global core_timer_enable
core_timer_enable:
  mov x0, 1
  msr cntp_ctl_el0, x0 // enable
  mrs x0, cntfrq_el0
  lsr x0, x0, #5 // Lab 5 preemption period: core frequency >> 5
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
  mov x0, sp              // pass the saved trap frame
  bl irq_handler_function
  bl prepare_user_return  // inspect the thread selected by the IRQ scheduler
  mov sp, x0              // the timer may switch to another user thread
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
