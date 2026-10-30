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
#define NR_MMAP 10
#define NR_SIGRETURN 11

/* mmap flags. */
#define PROT_NONE      0x0
#define PROT_READ      0x1
#define PROT_WRITE     0x2
#define PROT_EXEC      0x4
#define MAP_PRIVATE    0x02
#define MAP_ANONYMOUS  0x20
#define MAP_POPULATE   0x8000
#define MMAP_ACCESS_READ  0
#define MMAP_ACCESS_WRITE 1
#define MMAP_ACCESS_EXEC  2
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
uint64_t sys_mmap(uint64_t addr, uint64_t len, int prot, int flags,
                  int fd, uint64_t file_offset);
int sys_mmap_clone(thread_t *source, thread_t *dest);
void sys_mmap_release(thread_t *thread);
int sys_mmap_fault(thread_t *thread, uint64_t address, int access);
user_mmap_region_t *vm_region_create(uint64_t start, uint64_t length,
                                     unsigned int prot, unsigned int flags,
                                     const void *backing,
                                     uint64_t backing_size);
void vm_region_list_free(user_mmap_region_t *regions);

/* Last common step before load_all; may replace tf after SIGKILL. */
trap_frame_t *prepare_user_return(trap_frame_t *tf);

void sigreg(int SIGNAL, void (*handler)());
void sigkill(int pid, int SIGNAL);
void sigret(void);

#endif
