    .section .text
    .align  2
    .global user_main

user_main:
    // 1) fork()
    mov     x8, #4        // syscall #4 = fork
    svc     #0            // parent: x0=child pid, child: x0=0

    // fork 的回傳值必須在任何 uart_write 前保存。
    // parent: x19 = child PID；child: x19 = 0。
    mov     x19, x0
    cbz     x19, .child
    b       .parent

    // ------------ child ------------

.child:
    // 這個訊息代表 child 已經被 scheduler 切換並開始執行。
    adr     x0, child_msg
    mov     x1, #6        // len("CHILD\n")
    mov     x8, #2        // uart_write
    svc     #0

    // 2) exec("hello_exec.bin", NULL)
    adr     x0, prog_name // x0 = &"hello_exec.bin"
    mov     x1, #0        // x1 = argv=NULL
    mov     x8, #3        // syscall #3 = exec
    svc     #0            // 如果成功，不会返回到这里

    // 3) exec 失败，打印错误
    adr     x0, err_msg
    mov     x1, #21       // len("exec failed in child\n")==21
    mov     x8, #2        // uart_write
    svc     #0

    b       .child_hang

.parent:
    // ------------ parent ------------

    // （可选）打印一下 child pid
    adr     x0, parent_msg
    mov     x1, #15       // len("in parent, pid=")==15
    mov     x8, #2
    svc     #0

    // x19 仍然保存 fork() 回傳的 child PID。
    adr     x0, num_buf
    mov     w1, w19
    add     w1, w1, #'0'
    strb    w1, [x0]
    mov     x1, #2        // 打印数字 + '\n'
    mov     x8, #2
    svc     #0

    b       .parent_hang

// 无限循环
.child_hang:   
    b .child_hang
.parent_hang:
    b .parent_hang

    .section .rodata
prog_name:
    .asciz "hello_exec.bin"
child_msg:
    .ascii "CHILD\n"
err_msg:
    .ascii "exec failed in child\n"
parent_msg:
    .ascii "in parent, pid="
num_buf:
    .byte 0, '\n'
