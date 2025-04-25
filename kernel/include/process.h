// process.h
#ifndef PROCESS_H
#define PROCESS_H

#include "thread.h"       // 含 thread_context_t
#include "exception_.h"    // 含 trap_frame_t

typedef enum {
    PROC_READY,
    PROC_RUNNING,
    PROC_ZOMBIE,
} proc_status_t;

typedef struct process {
    int              pid;         // 進程識別碼
    proc_status_t    status;      // READY / RUNNING / ZOMBIE
    // ───────────核心切換用的 context───────────
    thread_context_t context;     // 保存 EL1 核心態的 callee-saved registers + SP/PC/PSTATE
    // ───────────例外／syscall 用的 trap frame───────────
    trap_frame_t   *trapframe;    // 指向擺在 kernel stack 頂的快照
    // ───────────地址空間（MMU）───────────
    void           *pagetable;    // page table 根指標（如果你還沒做 MMU，只先設為 NULL）
    // ───────────鏈結與資源───────────
    struct process *parent;       // fork 時用
    struct process *next;         // ready queue 用
    // （將來可擴充：open file table、signal handlers…）
} process_t;

extern process_t *current;    // 正在執行的 process
extern process_t *ready_queue;

#endif // PROCESS_H
