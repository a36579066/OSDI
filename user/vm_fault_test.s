    .section .text
    .align  2
    .global _start

/* A write to a read-only VMA must terminate the process. */
_start:
    mov     x0, #0
    mov     x1, #4096
    mov     x2, #1                  // PROT_READ only.
    mov     x3, #0x22
    mov     x4, #-1
    mov     x5, #0
    mov     x8, #10
    svc     #0
    cmn     x0, #1
    b.eq    unexpected
    mov     x1, #1
    str     x1, [x0]                // Must fault and be rejected.

unexpected:
    adr     x0, fail_msg
    mov     x1, #19
    mov     x8, #2
    svc     #0
    mov     x0, #0
    mov     x8, #5
    svc     #0
1:
    b       1b

    .section .rodata
fail_msg:
    .ascii  "RO protection FAIL\n"
