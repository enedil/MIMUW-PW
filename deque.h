#ifndef DEQUE_H
#define DEQUE_H

#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>

#define DEF_DEQUE(type)                                                     \
    typedef struct node_##type {                                            \
        struct node_##type * prev, * next;                                  \
        type val;                                                           \
    } node_##type##_t;                                                      \
                                                                            \
    typedef struct deque_##type {                                           \
        sem_t lock;                                                         \
        sem_t on_retrieve;                                                  \
        node_##type##_t begin, end;                                         \
    } deque_##type##_t;                                                     \
                                                                            \
    int deque_##type##_init(deque_##type##_t * d) {                         \
        int err = sem_init(&d->on_retrieve, 0, 0);                          \
        if (err != 0)                                                       \
            return err;                                                     \
        err = sem_init(&d->lock, 0, 0);                                     \
        if (err != 0) {                                                     \
            return err + 2*sem_destroy(&d->on_retrieve);                    \
        }                                                                   \
        d->begin.prev = d->end.next = NULL;                                 \
        d->begin.next = &d->end;                                            \
        d->end.prev = &d->begin;                                            \
        return 0;                                                           \
    }                                                                       \
                                                                            \
    int deque_##type##_is_empty(deque_##type##_t * d) {                     \
        return d->begin.next == &d->end;                                    \        
    }                                                                       \
                                                                            \
    int deque_##type##_push_front(deque_##type##_t * d, type val) {         \
        int err;                                                            \
        if ((err = sem_wait(&d->lock)) != 0) {                              \
            return err;                                                     \
        }                                                                   \
                                                                            \
        err = sem_post(&d->on_retrieve);                                \
        return err;                                                         \
    }


#endif
