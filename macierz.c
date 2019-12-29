#include <stdio.h>
#include <unistd.h>
#include "threadpool.h"

void f(void* arg, size_t size) {
    int* t = arg;
    printf("start %d\n", t[0]);
    sleep(5-t[0]);
    printf("end %d\n", t[0]);
}

int times[] = {1, 2, 3, 4, 5};

int main() {
    struct thread_pool t;
    if (thread_pool_init(&t, 4)) {
        return 1;
    }
    for (int i = 0; i < 5; ++i) {
        runnable_t r = {f, times + i, sizeof(int)};
        defer(&t, r);
    }
    sleep(6);
    thread_pool_destroy(&t);
}
