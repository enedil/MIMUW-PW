#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <stddef.h>
#include <pthread.h>
#include <semaphore.h>

typedef struct runnable {
  void (*function)(void *, size_t);
  void *arg;
  size_t argsz;
} runnable_t;

#define OK (0)
#define ERR (-1)
#define DEQUE_EMPTY (1)

typedef struct node {
    struct node *prev, *next;
    runnable_t val;
} node_t;

typedef struct deque {
    size_t size;
    node_t begin, end;
} deque_t;

typedef struct blocking_deque {
    deque_t deque;
    pthread_mutex_t lock;
    pthread_mutexattr_t lock_attr;
    sem_t sem;
} blocking_deque_t;

typedef struct thread_pool {
    int allow_adding;
    sem_t active_task_counter;
    size_t pool_size;
    pthread_t* threads;
    blocking_deque_t tasks;
} thread_pool_t;

int thread_pool_init(thread_pool_t *pool, size_t pool_size);

void thread_pool_destroy(thread_pool_t *pool);

int defer(thread_pool_t *pool, runnable_t runnable);

#endif
