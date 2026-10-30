    .section .text
    .align  2
    .global _start
    .global user_main

/* Minimal signal round-trip test:
 *   signal(1, handler) -> kill(getpid(), 1) -> handler -> sigreturn -> resume
 */
_start:
user_main:
    mov     x0, #1
    adr     x1, signal_handler
    mov     x8, #8                 // signal(1, signal_handler)
    svc     #0

    mov     x8, #0                 // getpid()
    svc     #0
    mov     x1, #1
    mov     x8, #9                 // kill(pid, 1)
    svc     #0

    // This instruction is reached only after handler returns via sigreturn.
    adr     x0, resumed_msg
    mov     x1, #8
    mov     x8, #2                 // uart_write("RESUMED\\n", 8)
    svc     #0

    mov     x0, #0
    mov     x8, #5                 // exit(0)
    svc     #0
1:
    b       1b

signal_handler:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    adr     x0, handler_msg
    mov     x1, #8
    mov     x8, #2                 // uart_write("HANDLER\\n", 8)
    svc     #0
    ldp     x29, x30, [sp], #16
    ret                             // x30 is the kernel-written trampoline

    .section .rodata
handler_msg:
    .ascii  "HANDLER\n"
resumed_msg:
    .ascii  "RESUMED\n"
