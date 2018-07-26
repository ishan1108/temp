#include <stdio.h>
#include <pthread.h>
#include "malloc.h"

#define N_THREAD 20
#define N_ALLOC 20

void *dataFunc(){
    int *data[N_ALLOC];
    for(int i = 0; i < N_ALLOC; i++){
        data[i] = (int*)malloc(500 + i);
    }
    
    for(int i = 0; i < N_ALLOC; i++){
        free(data[i]);
    }
    
}

int main(int argc, char **argv){
    
    int numThreads = N_THREAD;
    int i = 0;
    pthread_t threads[numThreads];
    
    for(int i = 0; i < numThreads; i++){
        pthread_create(threads + i,NULL, dataFunc,NULL);
        printf("==============Thread %d created===========\n",i);
    }
    
    for(i = 0; i < numThreads; i++){
        pthread_join(threads[i], NULL);
    }
    
    return 0;
}

