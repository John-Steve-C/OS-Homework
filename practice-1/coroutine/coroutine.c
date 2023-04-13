/* YOUR CODE HERE */

#include "coroutine.h"
#include <ucontext.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#define STACK_SIZE (1024)
//const int capacity = MAXN;

struct coroutine {
    coroutine_func func;
    ucontext_t ctx;
    int status, id, ret_value;
    ptrdiff_t size, stack_size;    // 保存指针相减的结果
    struct coroutine_pool *pool;
    char *stack;        // 每个协程的栈指针
    struct coroutine *father;   // 创造它的父协程
};

struct coroutine_pool {    // coroutine_pool
    char stack[1024 * STACK_SIZE];    // 当前协程池分配的栈空间
    ucontext_t main;
    int cur_co_id;            // 当前运行的协程
    int cnt, size, running_cnt; // 当前运行的协程总数
    struct coroutine **co;  // 二维指针,不直接存值
};

struct coroutine_pool *cur_pool = NULL;
// 理论上进入新线程时就需要新的 pool

void pool_init() {
    // open a coroutine_pool
    // TODO: 可能需要修改
    cur_pool = malloc(sizeof(*cur_pool));
    cur_pool->cur_co_id = -1;
    cur_pool->cnt = 0;
    cur_pool->size = 16;
    cur_pool->running_cnt = 0;
    cur_pool->co = malloc(cur_pool->size * sizeof(struct coroutine *));
    memset(cur_pool->co, 0, cur_pool->size * sizeof(struct coroutine *));
}

// 创建新协程
int co_new(coroutine_func func) {
    struct coroutine *co = malloc(sizeof(*co));
    co->func = func;
    co->pool = cur_pool;
    co->id = cur_pool->cnt++;
    co->status = READY;
    co->stack = NULL;
    co->size = 0;
    co->stack_size = 0;
    co->ret_value = 0;
    if (cur_pool->cur_co_id == -1) co->father = NULL;
    else co->father = cur_pool->co[cur_pool->cur_co_id];

    // double space
    if (cur_pool->cnt >= cur_pool->size) {
        int size = cur_pool->size;
        cur_pool->size *= 2;
        cur_pool->co = realloc(cur_pool->co, 2 * size * sizeof(struct coroutine *));
        memset(cur_pool->co + size, 0, size * sizeof(struct coroutine *));
        cur_pool->co[size] = co;
        return size;
    } else {
        // normal insert
        for (int i = 0; i < cur_pool->size; ++i) {
            if (cur_pool->co[i] == NULL) {
                cur_pool->co[i] = co;
                return i;
            }
        }
    }

    return -1;
}

void co_delete(struct coroutine *co) {
    free(co->stack);
    free(co);
}

void co_close() {
    for (int i = 0; i < cur_pool->size; ++i) {
        struct coroutine *co = cur_pool->co[i];
        if (co) co_delete(co);
    }
    free(cur_pool->co);
    cur_pool->co = NULL;
    free(cur_pool);
    cur_pool = NULL;
}

ucontext_t *get_father_ctx(struct coroutine* co) {
    if (co->father == NULL) return &cur_pool->main;
    else return &co->father->ctx;
}

static void execute() {
//    uintptr_t ptr = (uintptr_t) low32 | ((uintptr_t) hi32 << 32);
//    struct coroutine_pool *pool = (struct coroutine_pool *) ptr;
    struct coroutine *co = cur_pool->co[cur_pool->cur_co_id];

    co->ret_value = co->func();
    co->status = FINISHED;
    cur_pool->cur_co_id = -1;
    cur_pool->running_cnt--;

    // auto delete the coroutine
//    co_delete(co);
//    pool->co[id] = NULL;
//    --pool->cnt;

    swapcontext(&co->ctx, get_father_ctx(co));
}

void co_resume(int id) {
    struct coroutine *co = cur_pool->co[id];
    if (co == NULL) return;
    switch (co->status) {
        case READY:
            getcontext(&co->ctx);   // 保存当前上下文到 co->ctx
            // 分配栈空间
            // can't be the same
            co->stack = co->ctx.uc_stack.ss_sp = cur_pool->stack + STACK_SIZE * cur_pool->running_cnt;
            co->ctx.uc_stack.ss_size = STACK_SIZE;
            co->ctx.uc_link = get_father_ctx(co);
            co->status = RUNNING;
            cur_pool->running_cnt++;
            cur_pool->cur_co_id = id;

//            uintptr_t ptr = (uintptr_t) cur_pool;
            // 入口为 execute，执行后返回 uc->link
            // 相当于创建一个新的 context
            makecontext(&co->ctx, (void (*)(void)) execute, 0);
            // 当前 ctx 保存到 father,载入 co->ctx
            // 实际上就是执行上面的 execute 函数
            swapcontext(get_father_ctx(co), &co->ctx);

            break;
        case SUSPEND:
            memcpy(cur_pool->stack + STACK_SIZE - co->size, co->stack, co->size);

            co->status = RUNNING;
            cur_pool->cur_co_id = id;
            swapcontext(get_father_ctx(co), &co->ctx);
            break;
        default:
            assert(0);
    }
}

int co_start(int (*routine)(void)) {
    // pool init
    if (cur_pool == NULL) pool_init();

    // new
    int id = co_new(routine);
    assert(id != -1);

    // running at once after it's created
    co_resume(id);

    // block previous coroutine
//    for (int i = 0;i < cur_pool->cnt; ++i) {
//        if (cur_pool->co[i] != NULL && cur_pool->co[i]->status != FINISHED) cur_pool->co[i]->status = SUSPEND;
//    }
    return id;
}

int co_getid() {
    return cur_pool->cur_co_id;
}

int co_getret(int cid) {
    return (cur_pool->co[cid]->ret_value);
}

static void _save_stack(struct coroutine *co, char *top) {
    char dummy = 0;
    assert(top - &dummy <= STACK_SIZE);
    if (co->stack_size < top - &dummy) {
        free(co->stack);
        co->stack_size = top - &dummy;
        co->stack = malloc(co->stack_size);
    }
    co->size = top - &dummy;
    memcpy(co->stack, &dummy, co->size);
}

int co_yield() {
//    assert(cur_pool->cur_co_id >= 0);
    if (cur_pool->cur_co_id >= 0) {
        struct coroutine *cur_co = cur_pool->co[cur_pool->cur_co_id];
        // TODO: 如何暂存当前栈空间？
//        _save_stack(cur_co, cur_pool->stack + STACK_SIZE);

        cur_co->status = SUSPEND;
        if (cur_co->father == NULL) cur_pool->cur_co_id = -1;
        else cur_pool->cur_co_id = cur_co->father->id;
        swapcontext(&cur_co->ctx, get_father_ctx(cur_co));
    } else {
        for (int i = 0;i < cur_pool->cnt; ++i) {
            if (cur_pool->co[i]->status != FINISHED) {
                co_resume(i);
                break;
            }
        }
    }
}

int co_waitall() {
    for (int i = cur_pool->size - 1; i >= 0; --i) {
        if (cur_pool->co[i] != NULL) co_wait(i);
    }
}

int co_wait(int cid) {
//    for (int i = 0;i < cur_pool->size; ++i) {
//        if (i != cid && cur_pool->co[i] != NULL && cur_pool->co[i]->status != FINISHED) {
//            cur_pool->co[i]->status = SUSPEND;
//        }
//    }
    if (cur_pool->co[cid]->status != FINISHED) co_resume(cid);
}

int co_status(int cid) {
    if (cur_pool->co[cid] == NULL) {
        return UNAUTHORIZED;
    } else {
        return cur_pool->co[cid]->status;
    }
}