#ifndef SYSCALL_H
#define SYSCALL_H

#include "exception_.h"
#include "thread.h"
#include "uart.h"
#include "cpio_.h"
#include "mailbox.h"


extern trap_frame_t *current_tf;
extern void load_context(void *context);
#define NR_SIGNALS 64
#define SIGKILL 9
#define NR_SIGRETURN 10
int test_syscall();

// sysnum = 0
int getpid();

//sysnum = 1
size_t sys_uart_read(char *buf, size_t size);

// sysnum = 2
size_t sys_uart_write(const char *buf, size_t size);

// sysnum = 3
int sys_exec(trap_frame_t *tf, const char *name, char *const argv[]);

// sysnum = 4
int sys_fork(trap_frame_t *tf);
int fork(trap_frame_t *tf);

int exec(const char* name, char *const argv[]);

int sys_mbox_call(unsigned char ch, unsigned int *mbox);
int sys_kill(int pid);
int sys_signal(int signal, uint64_t handler);
int sys_signal_kill(int pid, int signal);
trap_frame_t *sys_sigreturn(trap_frame_t *tf);

/* Last common step before load_all; may replace tf after SIGKILL. */
trap_frame_t *prepare_user_return(trap_frame_t *tf);

void sigreg(int SIGNAL, void (*handler)());
void sigkill(int pid, int SIGNAL);
void sigret(void);

#endif
