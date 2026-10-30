#ifndef THREAD_H
#define THREAD_H

#include "allocator.h"
#include "lock.h"
#include "uart.h"
#include "utils.h"
#include "exception_.h"
// #include "mem.h"

#define THREAD_STK_SIZE 4096
#define USER_STACK_SIZE 16384
#define THREAD_SIGNAL_COUNT 64
#define SIGNAL_STACK_SIZE 4096
#define THREAD_NO_ACTIVE_SIGNAL (-1)

extern int next_pid;

typedef struct user_mmap_region {
    uint64_t start;
    uint64_t length;
    uint64_t page_count;
    unsigned int prot;
    unsigned int flags;
    const uint8_t *backing;
    uint64_t backing_size;
    struct user_mmap_region *next;
} user_mmap_region_t;

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
    THREAD_BLOCKED = -2,
} thread_status_t;

typedef struct thread {
    thread_context_t context;
    char *sp;
    struct thread *next;
    struct thread *prev;
    unsigned int user_stack_size;
    thread_status_t status;
    int pid;
    int is_user;

    /* Signal registration and delivery state.  The Lab does not require
     * nested handlers, so one saved user context and one handler stack are
     * sufficient for each user thread. */
    uint64_t signal_handlers[THREAD_SIGNAL_COUNT];
    uint64_t pending_signals;       // Bit n: signal n is waiting to be delivered.
    int signal_active;              // A user signal handler is currently running.
    int active_signal;              // Signal number for the active handler, or none.
    trap_frame_t saved_signal_tf;   // User context to restore on sigreturn().

    /* Only user threads need a trap frame: it is the EL0 state restored by
     * eret.  Kernel threads resume from context instead. */
    trap_frame_t  *trapframe;
    void          *pagetable; // Physical address of this thread's TTBR0 root.
    user_mmap_region_t *mmap_regions;
    uint64_t mmap_next_addr;
} thread_t;

extern thread_t *cur_thread;
extern thread_t *run_queue;

// context switch
extern void switch_to(void *prev, void *next);
// 進 EL0 的 eret stub
void eret_to_user(void);
// EL0 exception return bridge: restore the selected kernel thread context.
void exception_to_kernel(void);
void ret_from_fork(void);

extern void *get_current(void);

// kernel thread
void thread_init(void);
thread_t *thread_create(void *fn, void *arg);
void schedule(void);
void kill_zombies(void);
void idle_task(void);
int has_live_user_thread(void);
void foo(void);
void init_user_prog(void);

// Exception-driven scheduling for user processes.
trap_frame_t *schedule_from_irq(trap_frame_t *tf);
trap_frame_t *exit_current_thread(trap_frame_t *tf, int status);
trap_frame_t *exit_user_session(trap_frame_t *tf, int status);

// user process
thread_t *process_create(uint64_t entry_point, uint64_t user_sp,
                         void *pagetable);

#endif // THREAD_H
