    .section .text
    .align  2
    .global _start

/* Demand paging plus fork() copy-on-write isolation. */
_start:
    // The first instruction fetch and this stack read should each fault in.
    sub     sp, sp, #16
    ldr     x0, [sp]
    cbnz    x0, fail
    add     sp, sp, #16

    mov     x0, #0                  // Auto-addressed anonymous mapping.
    mov     x1, #4096
    mov     x2, #3                  // PROT_READ | PROT_WRITE.
    mov     x3, #0x22               // MAP_PRIVATE | MAP_ANONYMOUS.
    mov     x4, #-1
    mov     x5, #0
    mov     x8, #10                 // mmap.
    svc     #0
    cmn     x0, #1
    b.eq    fail
    mov     x19, x0

    ldr     x1, [x19]               // First touch must demand-map zero page.
    cbnz    x1, fail
    movz    x1, #0x7788
    movk    x1, #0x5566, lsl #16
    movk    x1, #0x3344, lsl #32
    movk    x1, #0x1122, lsl #48
    str     x1, [x19]

    mov     x8, #4                  // fork.
    svc     #0
    cbz     x0, child

    // Wait long enough for the preemption timer to run the child.
    mrs     x20, cntfrq_el0
    lsr     x20, x20, #3            // 125 ms in counter ticks.
    mrs     x21, cntpct_el0
    add     x20, x20, x21
wait_child:
    mrs     x21, cntpct_el0
    cmp     x21, x20
    b.lo    wait_child

    // Child's store must not change the parent's shared virtual page.
    ldr     x1, [x19]
    movz    x2, #0x7788
    movk    x2, #0x5566, lsl #16
    movk    x2, #0x3344, lsl #32
    movk    x2, #0x1122, lsl #48
    cmp     x1, x2
    b.ne    fail

    adr     x0, parent_pass
    mov     x1, #16
    mov     x8, #2                  // uart_write.
    svc     #0
    b       finish

child:
    movz    x1, #0x2211
    movk    x1, #0x4433, lsl #16
    movk    x1, #0x6655, lsl #32
    movk    x1, #0x8877, lsl #48
    str     x1, [x19]               // Write permission fault: copy this page.

    // Fork again while the page is already COW to exercise a 3-way refcount.
    mov     x8, #4
    svc     #0
    cbz     x0, grandchild

    movz    x1, #0xAA11
    movk    x1, #0xBB22, lsl #16
    movk    x1, #0xCC33, lsl #32
    movk    x1, #0xDD44, lsl #48
    str     x1, [x19]
    ldr     x2, [x19]
    cmp     x1, x2
    b.ne    child_fail
    adr     x0, child_pass
    mov     x1, #15
    mov     x8, #2
    svc     #0
child_wait_forever:
    b       child_wait_forever

grandchild:
    movz    x1, #0xEE11
    movk    x1, #0xFF22, lsl #16
    movk    x1, #0x1133, lsl #32
    movk    x1, #0x2244, lsl #48
    str     x1, [x19]
    ldr     x2, [x19]
    cmp     x1, x2
    b.ne    child_fail
    adr     x0, grandchild_pass
    mov     x1, #20
    mov     x8, #2
    svc     #0
    b       child_wait_forever

child_fail:
    adr     x0, child_fail_msg
    mov     x1, #15
    mov     x8, #2
    svc     #0
    b       child_wait_forever

fail:
    adr     x0, fail_msg
    mov     x1, #13
    mov     x8, #2
    svc     #0
finish:
    mov     x0, #0
    mov     x8, #5                  // End this user session.
    svc     #0
1:
    b       1b

    .section .rodata
parent_pass:
    .ascii  "COW parent PASS\n"
child_pass:
    .ascii  "COW child PASS\n"
grandchild_pass:
    .ascii  "COW grandchild PASS\n"
child_fail_msg:
    .ascii  "COW child FAIL\n"
fail_msg:
    .ascii  "VM test FAIL\n"
