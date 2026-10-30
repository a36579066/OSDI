    .section .text
    .align  2
    .global _start

/* An address outside every mapped region must terminate the process. */
_start:
    movz    x0, #0x9000, lsl #16    // 0x90000000, intentionally unmapped.
    ldr     x1, [x0]
    adr     x0, fail_msg
    mov     x1, #21
    mov     x8, #2
    svc     #0
    mov     x0, #0
    mov     x8, #5
    svc     #0
1:
    b       1b

    .section .rodata
fail_msg:
    .ascii  "unmapped access FAIL\n"
