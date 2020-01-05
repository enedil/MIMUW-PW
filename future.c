#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "future.h"

extern int robust_mutex_lock(pthread_mutex_t*);
extern int _mutex_init(pthread_mutex_t *, pthread_mutexattr_t *);
extern void _mutex_destroy(pthread_mutex_t *, pthread_mutexattr_t *);

typedef void *(*function_t)(void *);

void fatal_error(int e) {
    if (e) {
        void *array[10];
        size_t size;
        size = backtrace(array, 10);
        fprintf(stderr, "Stumbled upon fatal error.");
        backtrace_symbols_fd(array, size, 2);
        exit(1);
    }
}

static int future_init(future_t * future) {
    future->finished = 0;
    future->cont = (struct continuation){};
    int err = sem_init(&future->on_result, 0 /*pshared*/, 0 /*initial value*/);
    if (err)
        return err;
    if ((err = _mutex_init(&future->lock, &future->lock_attr))) {
        sem_destroy(&future->on_result);
        return err;
    }
    return OK;
}

void func_to_defer_async(void * ptr, __attribute__((unused)) size_t size) {
    future_t * future = ptr;
    sem_t * on_result = &future->on_result;
    callable_t * callable = &future->callable;

    void* result = callable->function(callable->arg, callable->argsz, &future->result_size);

    fatal_error(robust_mutex_lock(&future->lock));
    future->result = result;
    future->finished = 1;
    if (future->cont.exit_handler != NULL) {
        struct continuation cont = future->cont;
        future_t* task = cont.task;
        task->callable.arg = result;
        task->callable.argsz = future->result_size;

        size_t result_size = future->result_size;
        fatal_error(pthread_mutex_unlock(&future->lock));
//        cont->exit_handler(task, result_size);
        fatal_error(async(cont.pool_for_task, task, task->callable));

    } else {
        fatal_error(pthread_mutex_unlock(&future->lock));
        fatal_error(sem_post(on_result));
    }
}

int async(thread_pool_t *pool, future_t *future, callable_t callable) {
    int err = future_init(future);
    if (err)
        return err;
    future->callable = callable;
    runnable_t runnable = {.function = func_to_defer_async,
                           .arg = future,
                           .argsz = callable.argsz};
    return defer(pool, runnable);
}

int map(thread_pool_t *pool, future_t *future, future_t *from,
        void *(*function)(void *, size_t, size_t *)) {
    int err = future_init(future);
    if (err)
        return err;

    future->callable = (const callable_t){.function = function,
                                          .arg = NULL,
                                          .argsz = 0};
    err = robust_mutex_lock(&from->lock);
    if (err)
        return err;
    if (from->finished) {
        future->callable.arg = from->result;
        future->callable.argsz = from->result_size;
        runnable_t runnable = {.function = func_to_defer_async,
                               .arg = future,
                               .argsz = from->result_size};
        pthread_mutex_unlock(&from->lock);
        defer(pool, runnable);
    } else {
        struct continuation *cont = &from->cont;
        cont->exit_handler = func_to_defer_async;
        cont->task = future;
        pthread_mutex_unlock(&from->lock);
    }

    return 0;
}

void *await(future_t *future) {
    sem_t * on_result = &future->on_result;

    int err = 0;
    errno = 0;
    do {
        sem_wait(on_result);
    } while (err != 0 && errno != EINTR);
    fatal_error(err);
    fatal_error(sem_destroy(on_result));
    return future->result;
}
