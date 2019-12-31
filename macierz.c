#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "future.h"

typedef struct {
    int64_t value;
    int64_t delay;
} value_delay;

void* wait_then_ret_val(void* data, __attribute__((unused)) size_t sz, size_t* output_sz) {
    value_delay *ptr = data;
    *output_sz = sizeof(ptr->value);
    struct timespec t = {0, ptr->delay * 1000 * 100000};
    t.tv_sec += t.tv_nsec / (long)1e9;
    t.tv_nsec %= (long)1e9;
    do {
        errno = 0;
        nanosleep(&t, &t);
    } while (errno == EINTR);
    return &ptr->value;
}

int main() {
    struct thread_pool pool;
    if (thread_pool_init(&pool, 4)) {
        return EXIT_FAILURE;
    }
    size_t k, n;
    if (2 != scanf("%zu%zu", &k, &n))
        return EXIT_FAILURE;

    future_t *future_row = calloc(n, sizeof (future_t));
    value_delay *vd_row = calloc(n, sizeof (value_delay));
    if (future_row == NULL)
        return EXIT_FAILURE;

    callable_t callable = {wait_then_ret_val, NULL, sizeof(value_delay)};
    for (size_t i = 0; i < k; ++i) {
        for (size_t j = 0; j < n; ++j) {
            scanf("%ld%ld", &vd_row[j].value, &vd_row[j].delay);
            callable.arg = &vd_row[j];
            async(&pool, &future_row[j], callable);
        }
        int sum = 0;
        for (size_t j = 0; j < n ; ++j) {
            int* p = await(&future_row[j]);
            sum += *p;
        }
        printf("%d\n", sum);
    }

    free(vd_row);
    free(future_row);
    thread_pool_destroy(&pool);
    return EXIT_SUCCESS;
}
