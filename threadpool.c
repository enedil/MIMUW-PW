#include "threadpool.h"

int thread_pool_init(thread_pool_t *pool, size_t num_threads) { return 0; }

void thread_pool_destroy(struct thread_pool *pool) {}

int defer(struct thread_pool *pool, runnable_t runnable) { return 0; }
