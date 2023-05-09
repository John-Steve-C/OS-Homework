/* YOUR CODE HERE */
#ifndef COROUTINE_H
#define COROUTINE_H

typedef long long cid_t;
#define MAXN (50000)
#define UNAUTHORIZED (-1)
#define FINISHED (2)
#define RUNNING (1)
#define SUSPEND (3)
#define READY (0)

struct coroutine;
struct thread_data;  // schedule
struct schedule;
typedef int (*coroutine_func)(void);    // 定义函数指针
//typedef void (*coroutine_func)(struct coroutine_pool *, void *ud);    // 定义函数指针

int co_start(int (*routine)(void));     // 参数是(名为 routine,没有参数的)函数指针
int co_getid();
int co_getret(int cid);
int co_yield();
int co_waitall();
int co_wait(int cid);
int co_status(int cid);

void co_resume(int id);
//static void _save_stack(struct coroutine *co, char *top);

#endif