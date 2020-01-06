#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include "threadpool.h"

typedef struct {
    int64_t value;
    int64_t delay;
    sem_t* sem;
    int64_t* ptr;
} value_delay;

void wait_then_ret_val(void* data, __attribute__((unused)) size_t sz) {
    value_delay *ptr = data;
    long nanosecs = 1e9;
    long nanosec_per_millisec = 1000*1000;
    struct timespec t = {0, ptr->delay * nanosec_per_millisec};
    t.tv_sec += t.tv_nsec / nanosecs;
    t.tv_nsec %= nanosecs;
    do {
        errno = 0;
        nanosleep(&t, &t);
    } while (errno == EINTR);
    *ptr->ptr = ptr->value;
    if (sem_post(ptr->sem)) {
        exit(1);
    }
}

int main() {
    struct thread_pool pool;
    if (thread_pool_init(&pool, 4)) {
        return EXIT_FAILURE;
    }
    size_t k, n;
    if (2 != scanf("%zu%zu", &k, &n))
        return EXIT_FAILURE;

    int64_t** tab;
    tab = calloc(k, sizeof (*tab));
    value_delay **vd_row = calloc(k, sizeof (value_delay*));
    sem_t* sems = calloc(k, sizeof (sem_t));
    if (vd_row == NULL || sems == NULL || tab == NULL)
        return EXIT_FAILURE;
    for (size_t i = 0; i < k; ++i) {
        if (sem_init(sems + i, 0, 0)) {
            return EXIT_FAILURE;
        }
        tab[i] = calloc(n, sizeof (**tab));
        vd_row[i] = calloc(n, sizeof (**vd_row));
        if (tab[i] == NULL || vd_row[i] == NULL) {
            return EXIT_FAILURE;
        }
    }

    for (int i = 0; i < (int)k; ++i) {
        for (int j = 0; j < (int)n; ++j) {
            scanf("%ld%ld", &vd_row[i][j].value, &vd_row[i][j].delay);
            vd_row[i][j].ptr = &tab[i][j];
            vd_row[i][j].sem = sems + i;
            runnable_t runnable;
            runnable.arg = &vd_row[i][j];
            runnable.argsz = sizeof (vd_row);
            runnable.function = wait_then_ret_val;
            defer(&pool, runnable);
        }
    }

    for (size_t i = 0; i < k; ++i) {
        int sum = 0;
        for (size_t j = 0; j < n; ++j) {
            while (sem_wait(sems + i) != 0 || errno == EINTR);
        }
        for (size_t j = 0; j < n; ++j) {
            sum += tab[i][j];
        }
        printf("%d\n", sum);
    }

    for (size_t i = 0; i < k; ++i) {
        free(tab[i]);
        if (sem_destroy(sems + i)) {
            return EXIT_FAILURE;
        }
    }
    free(sems);
    free(tab);
    free(vd_row);
    thread_pool_destroy(&pool);
    return EXIT_SUCCESS;
}
