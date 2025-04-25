    .section .text
    .align  2
    .global user_main

user_main:
    // --- 1) getpid syscall (#0) ---
    mov     x8, #0        // syscall number = 0 (getpid)
    svc     #0            // 返回后，PID 在 x0

    // --- 2) 打印 "pid="  ---
    adr     x0, msg_pid   // x0 = &"pid="
    mov     x1, #4
    mov     x8, #2        // uart_write
    svc     #0

    // --- 3) 打印数字 + '\n' ---
    mov     x19, x0       // 保存 PID
    adr     x0, digit_buf
    add     w1, w19, #'0'
    strb    w1, [x0]
    // digit_buf[1] = '\n' 已在 .rodata 中初始化
    mov     x1, #2
    mov     x8, #2
    svc     #0

    // --- 4) 打印提示 "Input:"  ---
    adr     x0, msg_input
    mov     x1, #6        // len("Input:")==6
    mov     x8, #2
    svc     #0

    // --- 5) uart_read 到 buffer (最多64字节) ---
    adr     x0, buffer
    mov     x1, #64
    mov     x8, #1        // uart_read
    svc     #0            // 返回实际读到的字节数在 x0

    // --- 6) 回显刚才读到的字节 ---
    mov     x1, x0        // x1 = length
    adr     x0, buffer    // x0 = &buffer
    mov     x8, #2        // uart_write
    svc     #0
    // --- 8) 跳回 user_main 再次執行 ---
    b   user_main
    // --- 7) spin forever ---
1:  b   1b

    // -------------------------
    .section .bss
    .align  3
buffer:
    .space 64

    .section .rodata
msg_pid:
    .ascii "pid="

digit_buf:
    .byte 0
    .byte '\n'

msg_input:
    .ascii "Input:"

    // no .size needed
