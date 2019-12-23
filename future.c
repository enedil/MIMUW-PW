#include "future.h"

typedef void *(*function_t)(void *);

int async(thread_pool_t *pool, future_t *future, callable_t callable) {
  return 0;
}

int map(thread_pool_t *pool, future_t *future, future_t *from,
        void *(*function)(void *, size_t, size_t *)) {
  return 0;
}

void *await(future_t *future) { return 0; }
