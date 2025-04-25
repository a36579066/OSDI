    .section .text
    .align  2
    .global user_main
user_main:
    // 打印一行
    adr   x0, msg
    mov   x1, #17        // strlen("Hello from exec!\n")
    mov   x8, #2         // uart_write syscall
    svc   #0

    // 无限循环
1:  b     1b

    .section .rodata
msg:
    .ascii "Hello from exec!\n"
