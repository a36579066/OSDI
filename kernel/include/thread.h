#ifndef THREAD_H
#define THREAD_H

#include "allocator.h"
#include "lock.h"
#include "uart.h"
#include "utils.h"
#include "syscall.h"
// #include "mem.h"

#define THREAD_STK_SIZE 4096

extern int next_pid;

typedef struct thread_context {
    unsigned long long x19;
    unsigned long long x20;
    unsigned long long x21;
    unsigned long long x22;
    unsigned long long x23;
    unsigned long long x24;
    unsigned long long x25;
    unsigned long long x26;
    unsigned long long x27;
    unsigned long long x28;
    unsigned long long fp;
    unsigned long long lr;
    unsigned long long sp;
} thread_context_t;

typedef enum {
    THREAD_RUNNING = 1,
    THREAD_READY = 0,
    THREAD_ZOMBIE = -1,
} thread_status_t;

typedef struct thread {
    thread_context_t context;
    char *sp;
    struct thread *next;
    struct thread *prev;
    char *data;
    int data_size;
    thread_status_t status;
    int pid;
    trap_frame_t  *trapframe; // 在 kernel stack 頂，用來儲存 EL0 的寄存器
    void          *pagetable; // for lab6 or 7? 未來 MMU 時會用到，先留 NULL
} thread_t;

extern thread_t *cur_thread;
extern thread_t *run_queue;

// context switch
extern void switch_to(void *prev, void *next);
// 進 EL0 的 eret stub
void eret_to_user(void);
void ret_from_fork(void);

extern void *get_current(void);

// kernel thread
void thread_init(void);
thread_t *thread_create(void *fn, void *arg);
void schedule(void);
void kill_zombies(void);
void idle_task(void);
void foo(void);
void init_user_prog(void);

// user process
thread_t *process_create(uint64_t entry_point, uint64_t user_sp);

#endif // THREAD_H
