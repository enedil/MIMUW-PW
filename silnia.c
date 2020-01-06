#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

#include <unistd.h>

#include "future.h"

#define SIZE 0xCAFEBABE

static unsigned n;

struct data {
    uint64_t pref_prod;
    unsigned mult;
};

void* factorial_step(void* arg, __attribute__((unused)) size_t size, size_t* ressz) {
    struct data* d = arg;
    d->pref_prod *= d->mult;
    d->mult += 3;
    *ressz = SIZE;
    return d;
}

int main(){
    printf("pid: %d\n", getpid());
    if (1 != scanf("%u", &n))
        return EXIT_FAILURE;


    thread_pool_t pool;
    if (thread_pool_init(&pool, 3))
        return EXIT_FAILURE;


    future_t *fut = calloc(n, sizeof (*fut));
    if (fut == NULL)
        return EXIT_FAILURE;

    callable_t callable = {.function = factorial_step, .argsz = SIZE};

    struct data dane[3];
    for (unsigned i = 0; i < 3 && i < n; ++i) {
        dane[i] = (struct data){.mult = i+1, .pref_prod = 1};
        callable.arg = &dane[i];
        if (async(&pool, fut + i, callable))
            return EXIT_FAILURE;
    }

    for (unsigned i = 3; i < n; ++i) {
        if (map(&pool, fut + i, fut + i - 3, factorial_step))
            return EXIT_FAILURE;
    }

    uint64_t fact = 1;
    for (unsigned i = 0; i < 3 && n > i; ++i) {
        struct data* d = await(fut + n - i - 1);
        fact *= d->pref_prod;
    }
    printf("%" PRId64 "\n", fact);
    //thread_pool_destroy(&pool);
}
