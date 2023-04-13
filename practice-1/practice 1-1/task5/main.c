// ? Loc here: header modification to adapt pthread_setaffinity_np
#define _GNU_SOURCE

#include <assert.h>
#include <sched.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <utmpx.h>

cpu_set_t set;

void *thread1(void *dummy) {
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);

    assert(sched_getcpu() == 0);
    return NULL;
}

void *thread2(void *dummy) {
    assert(sched_getcpu() == 1);
    return NULL;
}

int main() {
    pthread_t pid[2];
    int i;
    // ? LoC: Bind core here
    CPU_ZERO(&set);
//    CPU_SET(0, &set);
//    CPU_SET(1, &set);
//    pthread_setaffinity_np(pid[0], sizeof(set), &set);
//    pthread_setaffinity_np(pid[1], sizeof(set), &set);

    for (i = 0; i < 2; ++i) {
        // 1 Loc code here: create thread and save in pid[2]
        pthread_create(&pid[i], NULL, thread1, NULL);
    }
    for (i = 0; i < 2; ++i) {
        // 1 Loc code here: join thread
        pthread_join(pid[i], NULL);
    }
    return 0;
}
