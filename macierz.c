#include <stdio.h>
#include <unistd.h>
#include "threadpool.h"

int main() {
    struct thread_pool t;
    if (thread_pool_init(&t, 4)) {
        return 1;
    }
    thread_pool_destroy(&t);
}
