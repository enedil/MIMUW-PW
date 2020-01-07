#include <stdlib.h>
#include <errno.h>

#include "future.h"

extern int robust_mutex_lock(pthread_mutex_t*);
extern int _mutex_init(pthread_mutex_t *, pthread_mutexattr_t *);
extern void _mutex_destroy(pthread_mutex_t *, pthread_mutexattr_t *);
static int async_internal(thread_pool_t *, future_t* , callable_t, int);

typedef void *(*function_t)(void *);


static int future_init(future_t * future) {
    future->finished = 0;
    future->cont = (__typeof((future->cont))){};
    int err = sem_init(&future->on_result, 0 /*pshared*/, 0 /*initial value*/);
    if (err)
        return err;
    if ((err = _mutex_init(&future->lock, &future->lock_attr))) {
        sem_destroy(&future->on_result);
        return err;
    }
    return OK;
}

static void future_destroy(future_t* future) {
    _mutex_destroy(&future->lock, &future->lock_attr);
    FE(sem_destroy(&future->on_result));
}

void func_to_defer_async(void * ptr, __attribute__((unused)) size_t size) {
    future_t * future = ptr;
    sem_t * on_result = &future->on_result;
    callable_t * callable = &future->callable;

    void* result = callable->function(callable->arg, callable->argsz, &future->result_size);

    FE(robust_mutex_lock(&future->lock));
    future->result = result;
    future->finished = 1;
    if (future->cont.exit_handler != NULL) {
        struct continuation cont = future->cont;
        future_t* task = cont.task;
        task->callable.arg = result;
        task->callable.argsz = future->result_size;

        FE(pthread_mutex_unlock(&future->lock));
        future_destroy(future);
        FE(async_internal(cont.pool_for_task, task, task->callable, 1));
    } else {
        FE(pthread_mutex_unlock(&future->lock));
        FE(sem_post(on_result));
    }
}


static int async_internal(thread_pool_t *pool, future_t* future, callable_t callable, int from_mapped) {
    if (!from_mapped) {
        int err = future_init(future);
        if (err)
        return err;
    }
    future->callable = callable;
    runnable_t runnable = {.function = func_to_defer_async,
                           .arg = future,
                           .argsz = callable.argsz};
    return defer(pool, runnable);
}

int async(thread_pool_t *pool, future_t *future, callable_t callable) {
    return async_internal(pool, future, callable, 0);
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
        return defer(pool, runnable);
    } else {
        struct continuation *cont = &from->cont;
        cont->exit_handler = func_to_defer_async;
        cont->task = future;
        cont->pool_for_task = pool;
        pthread_mutex_unlock(&from->lock);
    }

    return 0;
}

void *await(future_t *future) {
    sem_t * on_result = &future->on_result;

    int err = 0;
    errno = 0;
    int v;
    sem_getvalue(on_result, &v);
    do {
        err = sem_wait(on_result);
    } while (err != 0 && errno == EINTR);
    FE(err);
    future_destroy(future);
    return future->result;
}
