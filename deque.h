#ifndef DEQUE_H
#define DEQUE_H

#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>

/* Polymorphic Blocking Deque, specialized for type. Must be typedefed before inclusion. 
 */

#define ERR (-1)

typedef struct node {
    struct node *prev, *next;
    type val;
} node_t;

typedef struct deque {
    pthread_mutexattr_t lock_attr;
    pthread_mutex_t lock;
    sem_t on_retrieve;
    node_t begin, end;
} deque_t;

int deque_init(deque_t *d) {
    int err = 0;
    memset(d, 0, sizeof(*d));
    err = pthread_mutexattr_settype(&d->lock_attr, PTHREAD_MUTEX_ERRORCHECK);
    if (err)
        return err;
    err = sem_init(&d->on_retrieve, 0, 0);
    if (err)
        return err;
    err = pthread_mutex_init(&d->lock, &d->lock_attr);
    if (err)
        return sem_destroy(&d->on_retrieve), err;
    d->begin.prev = d->end.next = NULL;
    d->begin.next = &d->end;
    d->end.prev = &d->begin;
    return 0;
}

int deque_is_empty(deque_t *d) { return d->begin.next == &d->end; }

int deque_push_front(deque_t *d, type * val) {
    int err;
    node_t * new_node = malloc(sizeof(node_t));
    if (new_node == NULL) {
        return ERR;
    }
    new_node->val = *val;
    if ((err = pthread_mutex_lock(&d->lock)) != 0) {
        return err;
    }

    err = sem_post(&d->on_retrieve);
    if (err != 0) {
        return pthread_mutex_unlock(&d->lock);
    }
}

#endif
