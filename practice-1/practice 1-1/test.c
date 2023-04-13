// ? Loc here: header modification to adapt pthread_cond_t
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#define MAXTHREAD 10
// declare cond_variable: you may define MAXTHREAD variables
pthread_cond_t cond[MAXTHREAD];
pthread_mutex_t lock;
pthread_barrier_t barrier;

// ? Loc in thread1: you can do any modification here, but it should be less than 20 Locs
void *thread1(void* dummy){
    int i, index = *((int*) dummy);

    pthread_mutex_lock(&lock);
    // if (index < MAXTHREAD) {
    //     pthread_cond_wait(&cond[index], &lock[index]);
    // }

    printf("This is thread %d!\n", *((int*) dummy));
    for(i = 0; i < 20; ++i){
        printf("H");
        printf("e");
        printf("l");
        printf("l");
        printf("o");
        printf("W");
        printf("o");
        printf("r");
        printf("l");
        printf("d");
        printf("!");
    }
    printf("\n");

    // pthread_cond_signal(&cond[index]);
    pthread_mutex_unlock(&lock);
    pthread_barrier_wait(&barrier);

    return NULL;
}

int main(){
    pthread_t pid[MAXTHREAD];
    int i;
    // ? Locs: initialize the cond_variables
    for (i = 0; i < MAXTHREAD; ++i) {
        // pthread_cond_init(&cond[i], NULL);
    }
    pthread_mutex_init(&lock, NULL);
    pthread_barrier_init(&barrier, NULL, MAXTHREAD + 1);

    for(i = 0; i < MAXTHREAD; ++i){
        int* thr = (int*) malloc(sizeof(int)); 
        *thr = i;
        // 1 Loc here: create thread and pass thr as parameter
        pthread_create(&pid[i], NULL, thread1, thr);
    }
    pthread_barrier_wait(&barrier);

    for(i = 0; i < MAXTHREAD; ++i)
        // 1 Loc here: join thread
        pthread_join(pid[i], NULL);
    return 0;
}
