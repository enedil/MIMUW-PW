#ifndef FUTURE_H
#define FUTURE_H

#include <semaphore.h>

#include "threadpool.h"

typedef struct callable {
  void *(*function)(void *, size_t, size_t *);
  void *arg;
  size_t argsz;
} callable_t;

struct continuation {
    struct future* task;
    thread_pool_t* pool_for_task;
    void (*exit_handler)(void*, size_t);
};

typedef struct future {
    callable_t callable;
    sem_t on_result;
    pthread_mutex_t lock;
    pthread_mutexattr_t lock_attr;
    int finished;
    void* result;
    size_t result_size;
    struct continuation cont;
} future_t;

int async(thread_pool_t *pool, future_t *future, callable_t callable);

int map(thread_pool_t *pool, future_t *future, future_t *from,
        void *(*function)(void *, size_t, size_t *));

void *await(future_t *future);

#endif
