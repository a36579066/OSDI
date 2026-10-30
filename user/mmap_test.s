    .section .text
    .align  2
    .global _start

/* mmap smoke test. */
_start:
    // Zero-filled RW page.
    mov     x0, #0                  // Auto address.
    mov     x1, #4096
    mov     x2, #3                  // Read/write.
    mov     x3, #0x22               // Private anonymous.
    mov     x4, #-1
    mov     x5, #0
    mov     x8, #10                 // mmap.
    svc     #0
    cmn     x0, #1
    b.eq    fail
    mov     x19, x0
    ldr     x1, [x19]
    cbnz    x1, fail                // Must start zero.
    movz    x1, #0x5678
    movk    x1, #0x1234, lsl #16
    str     x1, [x19]
    ldr     x2, [x19]
    cmp     x1, x2
    b.ne    fail

    // Overlap hint moves.
    mov     x0, x19
    mov     x1, #4096
    mov     x2, #3
    mov     x3, #0x22
    mov     x4, #-1
    mov     x5, #0
    mov     x8, #10
    svc     #0
    cmn     x0, #1
    b.eq    fail
    cmp     x0, x19
    b.eq    fail
    mov     x20, x0
    ldr     x2, [x19]
    movz    x1, #0x5678
    movk    x1, #0x1234, lsl #16
    cmp     x1, x2
    b.ne    fail

    // Unaligned hint moves.
    add     x0, x19, #1
    mov     x1, #4096
    mov     x2, #3
    mov     x3, #0x22
    mov     x4, #-1
    mov     x5, #0
    mov     x8, #10
    svc     #0
    cmn     x0, #1
    b.eq    fail
    cmp     x0, x19
    b.eq    fail
    cmp     x0, x20
    b.eq    fail
    tst     x0, #0xfff
    b.ne    fail

    // Free hint stays exact.
    movz    x0, #0x10, lsl #16       // 1 MiB.
    mov     x1, #4096
    mov     x2, #1                  // Read-only.
    mov     x3, #0x22
    mov     x4, #-1
    mov     x5, #0
    mov     x8, #10
    svc     #0
    cmn     x0, #1
    b.eq    fail
    movz    x21, #0x10, lsl #16
    cmp     x0, x21
    b.ne    fail
    ldr     x1, [x0]                // Read check.
    cbnz    x1, fail

    adr     x0, pass_message
    mov     x1, #11
    mov     x8, #2                  // write.
    svc     #0
    mov     x0, #0
    mov     x8, #5                  // exit.
    svc     #0

fail:
    adr     x0, fail_message
    mov     x1, #10
    mov     x8, #2
    svc     #0
    mov     x0, #1
    mov     x8, #5
    svc     #0
1:
    b       1b

    .section .rodata
pass_message:
    .ascii  "mmap: PASS\n"
fail_message:
    .ascii  "mmap: FAIL\n"
