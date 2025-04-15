#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <stdint.h>

// 任務回呼函式型態，data 為傳入的參數
typedef void (*task_callback_t)();

// 任務結構，僅包含必要欄位
typedef struct task_t {
    task_callback_t callback;
    void *data;
    int priority;
    struct task_t *next;
    struct task_t *prev;
} task_t;

// 初始化任務隊列
void task_queue_init(void);

// 將任務加入隊列，採用 FIFO 原則
// 返回 1 表示成功，0 表示失敗
int task_enqueue(task_callback_t callback, void *data, int priority);

// 執行任務隊列中所有任務（FIFO 順序）
void task_execute(void);

#endif // TASK_QUEUE_H
