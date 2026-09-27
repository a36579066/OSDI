#ifndef SHELL_H
#define SHELL_H

#include "uart.h"
#include "string.h"
#define MAX_CMD_LEN 128  // 最大指令長度

void shell();
void async_shell();
void async_shell_notify_line_ready(void);
void kernel_shell_thread(void);
#endif
