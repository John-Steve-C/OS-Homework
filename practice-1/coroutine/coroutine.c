/* YOUR CODE HERE */

#include "coroutine.h"
#include <ucontext.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <stdio.h>

// the stack size of each thread(coroutine_pool)
#define STACK_SIZE (1<<16)
//const int capacity = MAXN;

struct coroutine {  // task struct of coroutine
    coroutine_func func;
    ucontext_t ctx;
    int status, id, ret_value;
    ptrdiff_t size, stack_size;    // 保存指针相减的结果

    char *stack;        // 每个协程的栈指针
    struct coroutine *father;   // 创造它的父协程
    struct thread_data *thread; // 属于哪个线程
};

struct thread_data {    // coroutine_pool
    char stack[1024 * STACK_SIZE];    // 当前协程池分配的栈空间
    ucontext_t main;
    int cur_co_id;            // 当前运行的协程
    int cnt, size, running_cnt; // 当前运行的协程总数
    struct coroutine **co;  // 二维指针,不直接存值
    pthread_t thread_id;
};

struct schedule {   // 最外层的结构
    struct thread_data **threads;
    int thread_cnt, thread_size; // 多个线程中的总协程数
};

struct schedule *sch = NULL;
// 只有一个大的 schedule

pthread_mutex_t lock_schedule, lock_thread, lock_coroutine;

struct thread_data * get_cur_thread_data() {
    pthread_t cur_thread = pthread_self();
    for (int i = 0;i < sch->thread_cnt; ++i) {
        if (sch->threads[i] != NULL && sch->threads[i]->thread_id == cur_thread)
            return sch->threads[i];
    }
    return NULL;
}

void schedule_init() {
    pthread_mutex_init(&lock_schedule, NULL);
    pthread_mutex_init(&lock_thread, NULL);
    pthread_mutex_init(&lock_coroutine, NULL);

    pthread_mutex_lock(&lock_schedule);

    sch = malloc(sizeof(*sch));

    sch->thread_cnt = 0;
    sch->thread_size = 16;
    sch->threads = malloc(sch->thread_size * sizeof(struct thread_data *));
    memset(sch->threads, 0, sch->thread_size * sizeof(struct thread_data *));

    pthread_mutex_unlock(&lock_schedule);
}

void thread_new(pthread_t pid) {
    pthread_mutex_lock(&lock_thread);

    struct thread_data * thr = malloc(sizeof(*thr)); // sizeof(thread_data)
    thr->thread_id = pid;
    thr->cur_co_id = -1;    // main coroutine
    thr->size = 16;
    thr->cnt = 0;
    thr->co = malloc(thr->size * sizeof(struct coroutine *));
    memset(thr->co, 0, thr->size * sizeof(struct coroutine *));

    if (sch->thread_cnt >= sch->thread_size) {
        // double space
        int size = sch->thread_size;
        sch->thread_size *= 2;
        sch->threads = realloc(sch->threads, 2 * size * sizeof(struct thread_data *));
        memset(sch->threads + size, 0, size * sizeof(struct thread_data *));
        sch->threads[size] = thr;
    } else {
        // normal insert
        sch->threads[sch->thread_cnt] = thr;
    }
    sch->thread_cnt++;

    pthread_mutex_unlock(&lock_thread);
}

// 创建新协程
int co_new(coroutine_func func) {
    pthread_mutex_lock(&lock_coroutine);

    struct thread_data *cur_thr = get_cur_thread_data();
    struct coroutine *co = malloc(sizeof(*co));
    co->func = func;
    co->thread = cur_thr;
    co->id = cur_thr->cnt;
    co->status = READY;
    co->stack = NULL;
    co->size = 0;
    co->stack_size = 0;
    co->ret_value = 0;
    if (co->thread->cur_co_id == -1) co->father = NULL;
    else co->father = cur_thr->co[cur_thr->cur_co_id];

    // double space
    if (cur_thr->cnt >= cur_thr->size) {
        int size = cur_thr->size;
        cur_thr->size *= 2;
        cur_thr->co = realloc(cur_thr->co, 2 * size * sizeof(struct coroutine *));
        memset(cur_thr->co + size, 0, size * sizeof(struct coroutine *));
        cur_thr->co[size] = co;
    } else {
        // normal insert
        cur_thr->co[cur_thr->cnt] = co;
    }
    cur_thr->cnt++;

    pthread_mutex_unlock(&lock_coroutine);

    return co->id;
}

void co_delete(struct coroutine *co) {
    free(co->stack);
    free(co);
}

//void co_close() {
//    for (int i = 0; i < sch->co_size; ++i) {
//        struct coroutine *co = sch->co[i];
//        if (co) co_delete(co);
//    }
//    free(sch->co);
//    sch->co = NULL;
//    free(sch);
//    sch = NULL;
//}

ucontext_t *get_father_ctx(struct coroutine* co) {
    if (co->father == NULL) return &co->thread->main;
    else return &co->father->ctx;
}

static void execute(uint32_t low32, uint32_t hi32) {
    uintptr_t ptr = (uintptr_t) low32 | ((uintptr_t) hi32 << 32);
    struct thread_data *thr = (struct thread_data *) ptr;
    struct coroutine *co = thr->co[thr->cur_co_id];

    co->ret_value = co->func();
    co->status = FINISHED;
    if (co->father == NULL) {
        co->thread->cur_co_id = -1;
//        co->thread->main_status = FINISHED;
    } else {
        // 执行完应该回退到父协程
        co->thread->cur_co_id = co->father->id;
    }
//    sch->co_running_cnt--;

//    printf("coroutine %d finished! And going to run co %d\n", co->id, co->thread->cur_co_id);

    // auto delete the coroutine
//    co_delete(co);
//    pool->co[id] = NULL;
//    --pool->cnt;

    // ctx->uc_link 已经保存, 不需要手动跳转？
//    swapcontext(&co->ctx, get_father_ctx(co));
}

void co_resume(int id) {
    // pthread_mutex_lock(&lock);

    struct thread_data *cur_thr = get_cur_thread_data();
    struct coroutine *co = cur_thr->co[id];
    if (co == NULL) return;

    // pthread_mutex_lock(&co->lock);

    switch (co->status) {
        case READY:
            getcontext(&co->ctx);   // 保存当前上下文到 co->ctx
            // 分配栈空间
            // can't be the same
            co->stack = co->ctx.uc_stack.ss_sp = cur_thr->stack + STACK_SIZE * cur_thr->cnt;
            co->ctx.uc_stack.ss_size = STACK_SIZE;
            co->ctx.uc_link = get_father_ctx(co);
            co->status = RUNNING;
//            sch->co_running_cnt++;
            co->thread->cur_co_id = id;

            uintptr_t ptr = (uintptr_t) cur_thr;
            // 入口为 execute，执行后返回 uc->link
            // 相当于创建一个新的 context
            makecontext(&co->ctx, (void (*)(void)) execute, 2, ptr, (uint32_t) (ptr >> 32));

            // pthread_mutex_unlock(&lock);

            // 当前 ctx 保存到 father,载入 co->ctx
            // 实际上就是执行上面的 execute 函数
            swapcontext(get_father_ctx(co), &co->ctx);

            // pthread_mutex_unlock(&co->lock);

            break;
        case SUSPEND:
//            memcpy(sch->stack + STACK_SIZE - co->size, co->stack, co->size);

            co->status = RUNNING;
            cur_thr->cur_co_id = id;

            // pthread_mutex_unlock(&lock);
            swapcontext(get_father_ctx(co), &co->ctx);
            // pthread_mutex_unlock(&co->lock);
            break;
        case RUNNING:
//            if (id == -1) {
//                co_yield();
//            } else {
//                co->thread->cur_co_id = id;
//                swapcontext(get_father_ctx(co), &co->ctx);
//            }
            break;
        default:
            assert(0);
            break;
    }

}

int co_start(int (*routine)(void)) {

    // schedule init
    if (sch == NULL) schedule_init();

    // new thread if we need
    pthread_t cur_thread = pthread_self();
    if (get_cur_thread_data() == NULL) thread_new(cur_thread);

//    printf("enter co_start by %d\n", get_cur_thread_data()->cur_co_id);

//    printf("cur_thread = %lu\n", cur_thread);
    // new
    int id = co_new(routine);
    assert(id != -1);

//    printf("new coroutine = %d \n", id);

    // running at once after it's created
//    co_yield();
    co_resume(id);

//    printf("%d is resumed\n", id);

    return id;
}

int co_getid() {
    if (get_cur_thread_data() == NULL) assert(0);
//    if (get_cur_thread_data()->cur_co_id == -1) co_yield();
    return get_cur_thread_data()->cur_co_id;
}

int co_getret(int cid) {
    // important! when query ret_value, it must be finished!
    struct thread_data *cur_thr = get_cur_thread_data();
    if (cur_thr->co[cid]->status != FINISHED) co_wait(cid);
    assert(cur_thr->co[cid]->status == FINISHED);
    return cur_thr->co[cid]->ret_value;
}

int co_yield() {
//    assert(cur_pool->cur_co_id >= 0);
    struct thread_data *cur_thread = get_cur_thread_data();
    int old_id = cur_thread->cur_co_id;

    if (cur_thread->cur_co_id >= 0) {
        struct coroutine *cur_co = cur_thread->co[cur_thread->cur_co_id];
        // TODO: 如何暂存当前栈空间？
//        _save_stack(cur_co, cur_pool->stack + STACK_SIZE);

        cur_co->status = SUSPEND;
        if (cur_co->father == NULL) cur_thread->cur_co_id = -1;
        else {
            int new_id = cur_co->father->id;
            cur_thread->cur_co_id = new_id;
        }
//        printf("yield co: %d -> co: %d\n", old_id, cur_thread->cur_co_id);
        swapcontext(&cur_co->ctx, get_father_ctx(cur_co));
    } else {
        // cur_co_id == -1, return to root
        for (int i = 0;i < cur_thread->cnt; ++i) {
            // yield 给同线程下的其他协程
            if (cur_thread->co[i]->status != FINISHED) {
//                printf("yield co: %d -> co: %d\n", old_id, i);
                co_resume(i);
                break;
            }
        }
    }
}

int co_waitall() {
    struct thread_data *cur_thr = get_cur_thread_data();
    for (int i = 0; i < cur_thr->size; ++i) {
        if (cur_thr->co[i] != NULL && cur_thr->co[i]->status != FINISHED) co_wait(i);
    }
}

int co_wait(int cid) {
//    for (int i = 0;i < cur_pool->size; ++i) {
//        if (i != cid && cur_pool->co[i] != NULL && cur_pool->co[i]->status != FINISHED) {
//            cur_pool->co[i]->status = SUSPEND;
//        }
//    }
//    sch->co[get_cur_thread_data()->cur_co_id]->status = SUSPEND;
    struct thread_data *cur_thr = get_cur_thread_data();
    while (cur_thr->co[cid]->status != FINISHED) co_resume(cid);
}

int co_status(int cid) {
    struct thread_data *cur_thr = get_cur_thread_data();
    if (cid == -1) { // main_coroutine
        return RUNNING; // main coroutine is always running?
    } else if (cur_thr->co[cid] == NULL || cur_thr->co[cid] != NULL && cur_thr->thread_id != pthread_self()) {
        return UNAUTHORIZED;
    } else {
        return cur_thr->co[cid]->status;
    }
}