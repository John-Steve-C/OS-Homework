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
#define STACK_SIZE (1<<20)
//const int capacity = MAXN;

struct coroutine {  // task struct of coroutine
    coroutine_func func;
    ucontext_t ctx;
    int status, id, ret_value;
    ptrdiff_t size, stack_size;    // 保存指针相减的结果

    char *stack;        // 每个协程的栈指针
    struct coroutine *father;   // 创造它的父协程
    struct thread_data *thread; // 属于哪个线程

    // pthread_mutex_t lock;
};

// coroutine_pool with one thread.
struct thread_data {
    char stack[2 * STACK_SIZE];    // 当前协程池分配的栈空间
    ucontext_t main;
    int cur_co_id;            // 当前运行的协程
    int cnt, size, running_cnt; // 当前运行的协程总数
    struct coroutine **co;  // 二维指针,不直接存值
    pthread_t thread_id;    // 属于哪个线程
};

struct schedule {   // 最外层的结构
    struct coroutine ***co;  // 三维指针,
    struct thread_data **threads;
    int thread_cnt, co_cnt; // 多个线程中的总协程数
    int thread_size, co_size;
    // , co_size, co_running_cnt;
};

struct schedule *sch = NULL;
// 只有一个大的 schedule

pthread_mutex_t lock_schedule, lock_thread, lock_coroutine;

struct thread_data *get_cur_thread_data() {
    pthread_t cur_thread = pthread_self();
    for (int i = 0;i < sch->thread_cnt; ++i) {
        if (sch->threads[i] != NULL && sch->threads[i]->thread_id == cur_thread)
            return sch->threads[i];
    }
    return NULL;
}

struct coroutine *get_cur_coroutine() {
    struct thread_data *cur_thr = get_cur_thread_data();
    if (cur_thr->cur_co_id == -1) return NULL;
    else return cur_thr->co[cur_thr->cur_co_id];
}

void schedule_init() {
    pthread_mutex_init(&lock_schedule, NULL);
    pthread_mutex_init(&lock_thread, NULL);
    pthread_mutex_init(&lock_coroutine, NULL);

    pthread_mutex_lock(&lock_schedule);

    sch = malloc(sizeof(*sch));

    sch->co_cnt = 0;
    sch->co_size = 16;
    sch->thread_cnt = 0;
    sch->thread_size = 16;
    sch->threads = malloc(sch->thread_size * sizeof(struct thread_data *));
    memset(sch->threads, 0, sch->thread_size * sizeof(struct thread_data *));


//    sch->co_cnt = 0;
//    sch->co_running_cnt = 0;
//    sch->co_size = 16;
    sch->co = malloc(sch->co_size * sizeof(struct coroutine **));
    memset(sch->co, 0, sch->co_size * sizeof(struct coroutine **));

    pthread_mutex_unlock(&lock_schedule);
}

//void pool_init() {
//    // open a coroutine_pool
//
//    cur_pool = malloc(sizeof(*cur_pool));
//    cur_pool->cur_co_id = -1;
//    cur_pool->cnt = 0;
//    cur_pool->size = 16;
//    cur_pool->running_cnt = 0;
//    cur_pool->co = malloc(cur_pool->size * sizeof(struct coroutine *));
//    memset(cur_pool->co, 0, cur_pool->size * sizeof(struct coroutine *));
//
////    pthread_mutex_init(&lock, NULL);
//}

void thread_new() {
    pthread_mutex_lock(&lock_thread);

    pthread_t pid = pthread_self();
    struct thread_data * new_thr = malloc(sizeof(*new_thr));
    new_thr->thread_id = pid;
    new_thr->cur_co_id = -1;    // main coroutine
    new_thr->cnt = 0;
    new_thr->running_cnt = 0;
    new_thr->size = 16;
    new_thr->co = malloc(new_thr->size * sizeof(struct coroutine *));
    memset(new_thr->co, 0, new_thr->size * sizeof(struct coroutine *));

    if (sch->thread_cnt >= sch->thread_size) {
        // double space
        int size = sch->thread_size;
        sch->thread_size *= 2;
        sch->threads = realloc(sch->threads, 2 * size * sizeof(struct thread_data *));
        memset(sch->threads + size, 0, size * sizeof(struct thread_data *));
        sch->threads[size] = new_thr;
    } else {
        // normal insert
        sch->threads[sch->thread_cnt] = new_thr;
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
    co->id = sch->co_cnt;
    co->status = READY;
    co->stack = NULL;
    co->size = co->stack_size = STACK_SIZE;  // modified!
    co->ret_value = 0;
    if (cur_thr->cur_co_id == -1) co->father = NULL;
    else co->father = cur_thr->co[cur_thr->cur_co_id];

    // pthread_mutex_init(&co->lock, NULL);

    // pthread_mutex_unlock(&lock);

    int pos = -1;
    // add to cur_thr, double space
    if (cur_thr->cnt >= cur_thr->size) {
        int size = cur_thr->size;
        cur_thr->size *= 2;
        cur_thr->co = realloc(cur_thr->co, 2 * size * sizeof(struct coroutine *));
        memset(cur_thr->co + size, 0, size * sizeof(struct coroutine *));
        cur_thr->co[size] = co;
        pos = size;
    } else {
        // normal insert
//        for (int i = 0; i < cur_thr->size; ++i) {
//            if (cur_thr->co[i] == NULL) {
//                cur_thr->co[i] = co;
//                pos = i;
//                break;
//            }
//        }
        cur_thr->co[cur_thr->cnt] = co;
        pos = cur_thr->cnt;
    }
    cur_thr->cnt++;

    // add to sch
    if (sch->co_cnt >= sch->co_size) {
        int size = sch->co_size;
        sch->co_size *= 2;
        sch->co = realloc(sch->co, 2 * size * sizeof(struct coroutine **));
        memset(sch->co + size, 0, size * sizeof(struct coroutine **));
        sch->co[size] = &cur_thr->co[pos];
    } else {
        sch->co[sch->co_cnt] = &cur_thr->co[pos];
    }
    sch->co_cnt++;


    pthread_mutex_unlock(&lock_coroutine);
    return co->id;
}

//void co_delete(struct coroutine *co) {
//    free(co->stack);
//    free(co);
//}
//
//__attribute__((destructor)) static void co_close() {
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
    struct thread_data * cur_thr = (struct thread_data *) ptr;
//    struct thread_data *cur_thr = get_cur_thread_data();

    assert(cur_thr->cur_co_id != -1);
    struct coroutine *co = cur_thr->co[cur_thr->cur_co_id];

    co->ret_value = co->func();
    printf("Function of %d completed!\n", co->id);
    co->status = FINISHED;
    struct coroutine *co_fa = co->father;
    if (co_fa == NULL) {
        cur_thr->cur_co_id = -1;
//        co->thread->main_status = FINISHED;
    } else {
        // 执行完应该回退到父协程
        cur_thr->cur_co_id = co->father->id;
    }
    cur_thr->running_cnt--;

    printf("coroutine %d finished! And prepare to switch to: %d\n", co->id, cur_thr->cur_co_id);

    if (co_fa != NULL) {
        memcpy(cur_thr->stack, co_fa->stack, STACK_SIZE);
        co_fa->status = RUNNING;
    }

    // auto delete the coroutine
//    co_delete(co);
//    pool->co[id] = NULL;
//    --pool->cnt;

    // ctx->uc_link 已经保存, 不需要手动跳转？
//    swapcontext(&co->ctx, get_father_ctx(co));
}

void co_resume(int id) {
    // pthread_mutex_lock(&lock);

    if (sch->co[id] == NULL) return;
    struct coroutine *co = *(sch->co[id]);
    struct thread_data *cur_thr = get_cur_thread_data();

    // pthread_mutex_lock(&co->lock);

    switch (co->status) {
        case READY:
            getcontext(&co->ctx);   // 保存当前上下文到 co->ctx
            // 分配栈空间
            // can't be the same?
            // copy co->stack back to cur_thr->stack while running co!
            co->ctx.uc_stack.ss_sp = cur_thr->stack;
            co->ctx.uc_stack.ss_size = STACK_SIZE;
            co->ctx.uc_link = get_father_ctx(co);
            co->status = RUNNING;
            cur_thr->running_cnt++;
            cur_thr->cur_co_id = id;

            co->stack = malloc(STACK_SIZE);

            uintptr_t ptr = (uintptr_t) cur_thr;
            // 入口为 execute，执行后返回 uc->link
            // 相当于创建一个新的 context
            makecontext(&co->ctx, (void (*)(void)) execute, 2, (uint32_t) ptr, (uint32_t) (ptr >> 32));

            // pthread_mutex_unlock(&lock);

            // 进入嵌套协程时，手动暂停父协程
            if (co->father != NULL) {
                co->father->status = SUSPEND;
                memcpy(cur_thr->stack, co->father->stack, STACK_SIZE);
            }

            // 当前 ctx 保存到 father,载入 co->ctx
            // 实际上就是执行上面的 execute 函数
            swapcontext(get_father_ctx(co), &co->ctx);

            // pthread_mutex_unlock(&co->lock);

            break;
        case SUSPEND:
            // cope with yield
            memcpy(cur_thr->stack, co->stack, STACK_SIZE);

            co->status = RUNNING;
            co->thread->cur_co_id = id;

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
    if (get_cur_thread_data() == NULL) thread_new();

//    printf("cur_thread = %lu\n", cur_thread);
    // new
    int id = co_new(routine);
    assert(id != -1);

    printf("new coroutine = %d \n", id);
//    printf("%d\n", sch->co_cnt);

    // running at once after it's created
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
//    if ( (*(sch->co[cid]))->status != FINISHED) (*(sch->co[cid]))->ret_value = (*(sch->co[cid]))->func();
    return (*(sch->co[cid]))->ret_value;
}

//static void _save_stack(struct coroutine *co, char *top, char *cur_sp) {
//    register char *esp asm ("esp");
//    char dummy = 0; // &dummy stands for current 'stack_top'
//    assert(top - esp < STACK_SIZE);
//    if (co->stack_size < top - esp) {
//        free(co->stack);
//        co->stack_size = top - esp;
//        co->stack = malloc(co->stack_size);
//    }
//    co->size = top - esp;
//    memcpy(co->stack, esp, co->size);
//}

int co_yield() {
//    assert(cur_pool->cur_co_id >= 0);
    struct thread_data *cur_thread = get_cur_thread_data();
    if (cur_thread->cur_co_id >= 0) {
        struct coroutine *cur_co = cur_thread->co[cur_thread->cur_co_id];
        // TODO: 如何暂存当前栈空间？
        // 调用 $esp
//        register char *esp asm ("esp");
//        _save_stack(cur_co, cur_thread->stack + STACK_SIZE, esp);

        memcpy(cur_co->stack, cur_thread->stack, STACK_SIZE);

        cur_co->status = SUSPEND;
        if (cur_co->father == NULL) cur_thread->cur_co_id = -1;
        else cur_thread->cur_co_id = cur_co->father->id;
        swapcontext(&cur_co->ctx, get_father_ctx(cur_co));
    } else {
        // cur_co_id == -1, return to root
        for (int i = 0;i < cur_thread->size; ++i) {
            // yield 给同线程下的其他协程
            if (cur_thread->co[i] != NULL && cur_thread->co[i]->status != FINISHED) {
                co_resume(cur_thread->co[i]->id);
                break;
            }
        }
    }
}

int co_waitall() {
    for (int i = 0; i < sch->co_cnt; ++i) {
        if (sch->co[i] != NULL && (*(sch->co[i]))->status != FINISHED) co_wait(i);
    }
}

int co_wait(int cid) {
//    for (int i = 0;i < cur_pool->size; ++i) {
//        if (i != cid && cur_pool->co[i] != NULL && cur_pool->co[i]->status != FINISHED) {
//            cur_pool->co[i]->status = SUSPEND;
//        }
//    }
    while ( (*(sch->co[cid]))->status != FINISHED) co_resume(cid);
}

int co_status(int cid) {
    if (cid == -1) { // main_coroutine
        return RUNNING; // main coroutine is always running?
    } else if (sch->co[cid] == NULL || sch->co[cid] != NULL && (*(sch->co[cid]))->thread->thread_id != pthread_self()) {
        return UNAUTHORIZED;
    } else {
        return (*(sch->co[cid]))->status;
    }
}