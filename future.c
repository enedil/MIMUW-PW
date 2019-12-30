#include <stdlib.h>
#include <errno.h>

#include "future.h"

typedef void *(*function_t)(void *);

void func_to_defer(void * ptr, __attribute__((unused)) size_t size) {
    future_t * future = ptr;
    sem_t * on_result = &future->on_result;
    callable_t * callable = &future->callable;

    future->result = callable->function(callable->arg, callable->argsz, &future->result_size);
    sem_post(on_result);
}

int async(thread_pool_t *pool, future_t *future, callable_t callable) {
    future->callable = callable;
    int err = sem_init(&future->on_result, 0 /*pshared*/, 0 /*initial value*/);
    if (err)
        return err;
    runnable_t runnable = {.function = func_to_defer,
                           .arg = future,
                           .argsz = sizeof (*future)};
    err = defer(pool, runnable);
    return err;
}

int map(thread_pool_t *pool, future_t *future, future_t *from,
        void *(*function)(void *, size_t, size_t *)) {
  return 0;
}

void *await(future_t *future) {
    sem_t * on_result = &future->on_result;
    callable_t * callable = &future->callable;

    int err = 0;
    errno = 0;
    do {
        sem_wait(on_result);
    } while (err == 0 || errno == EINTR);
    sem_destroy(on_result);
    return future->result;
}
