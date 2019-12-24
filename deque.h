#ifndef DEQUE_H
#define DEQUE_H

#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>

/* Polymorphic Blocking Deque, specialized for type. Must be typedefed before inclusion. 
 */

#define OK (0)
#define ERR (-1)
#define DEQUE_EMPTY (1)

typedef struct node {
    struct node *prev, *next;
    type val;
} node_t;

typedef struct deque {
    size_t size;
    node_t begin, end;
} deque_t;

typedef struct blocking_deque {
    deque_t deque;
    pthread_mutex_t lock;
    pthread_mutexattr_t lock_attr;
    pthread_cond_t cond;
} blocking_deque_t;

void deque_init(deque_t *d);
void deque_destroy(deque_t *d);
size_t deque_size(deque_t *d);
int deque_is_empty(deque_t *d); 
int deque_push_front(deque_t *d, type * val);
int deque_push_back(deque_t *d, type * val);
int deque_pop_front(deque_t *d, type * val);
int deque_pop_back(deque_t *d, type * val);

void deque_init(deque_t *d) {
    d->size = 0;
    d->begin.prev = d->end.next = NULL;
    d->begin.next = &d->end;
    d->end.prev = &d->begin;
}

void deque_destroy(deque_t *d) {
    for (node_t * ptr = d->begin.next; ptr != &d->end;) {
        node_t * next = ptr->next;
        free(ptr);
        ptr = next;
    }
}

size_t deque_size(deque_t *d) {
    return d->size;
}

int deque_is_empty(deque_t *d) { 
    return deque_size(d) == 0; 
}

int deque_push_front(deque_t *d, type * val) {
    node_t * new_node = malloc(sizeof(node_t));
    if (new_node == NULL) {
        return ERR;
    }
    d->size++;

    new_node->val = *val;
    new_node->prev = &d->begin;
    new_node->next = d->begin.next;
    d->begin.next = new_node;
    new_node->next->prev = new_node;
    return OK;
}

int deque_push_back(deque_t *d, type * val) {
    node_t * new_node = malloc(sizeof(node_t));
    if (new_node == NULL) {
        return ERR;
    }
    d->size++;

    new_node->val = *val;
    new_node->next = &d->end;
    new_node->prev = d->end.prev;
    d->end.prev = new_node;
    new_node->prev->next = new_node;
    return OK;
}

int deque_pop_front(deque_t *d, type * val) {
    if (deque_is_empty(d)) {
        return DEQUE_EMPTY;
    }
    d->size--;
    node_t * front = d->begin.next;
    *val = front->val;

    d->begin.next = front->next;
    front->next->prev = &d->begin;

    free(front);

    return OK;
}


int deque_pop_back(deque_t *d, type * val) {
    if (deque_is_empty(d)) {
        return DEQUE_EMPTY;
    }
    d->size--;
    node_t * back = d->end.prev;
    *val = back->val;

    d->end.prev = back->prev;
    back->prev->next = &d->end;

    free(back);

    return OK;
}

static int _mutexattr_init(pthread_mutexattr_t *attr) {
    int err;
    if ((err = pthread_mutexattr_init(attr))) {
        return err;
    }
    if ((err = pthread_mutexattr_settype(attr, PTHREAD_MUTEX_ERRORCHECK))) {
        pthread_mutexattr_destroy(attr);
        return err;
    }
    if ((err = pthread_mutexattr_setrobust(attr, PTHREAD_MUTEX_ROBUST))) {
        pthread_mutexattr_destroy(attr);
        return err;
    }
    return OK;
}

static int _mutex_init(pthread_mutex_t *mutex, pthread_mutexattr_t *attr) {
    int err;
    if ((err = _mutexattr_init(attr))) {
        return err;
    }
    if ((err = pthread_mutex_init(mutex, attr))) {
        pthread_mutexattr_destroy(attr);
        return err;
    }
    return OK;
}

static void _mutex_destroy(pthread_mutex_t *mutex, pthread_mutexattr_t *attr) {
    pthread_mutex_destroy(mutex);
    pthread_mutexattr_destroy(attr);
}

int blocking_deque_init(blocking_deque_t *d) {
    int err;
    memset(d, 0, sizeof(blocking_deque_t));
    if ((err = _mutex_init(&d->lock, &d->lock_attr))) {
        return err;
    }

    if ((err = pthread_cond_init(&d->cond, NULL))) {
        _mutex_destroy(&d->lock, &d->lock_attr);
        return err;
    }

    deque_init(&d->deque);
    return OK;
}

void blocking_deque_destroy(blocking_deque_t *d) {
    deque_destroy(&d->deque);
    _mutex_destroy(&d->lock, &d->lock_attr);
    pthread_cond_destroy(&d->cond);
}

size_t blocking_deque_size(blocking_deque_t *d) {
    size_t ret;
    pthread_mutex_lock(&d->lock);
    ret = deque_size(&d->deque);
    pthread_mutex_unlock(&d->lock);
    return ret;
}

int blocking_deque_is_empty(blocking_deque_t *d) {
    return blocking_deque_size(d) == 0;
}

int blocking_deque_push_front(blocking_deque_t *d, type * val) {
    int err;
    if ((err = pthread_mutex_lock(&d->lock)))
        return err;
    if ((err = deque_push_front(&d->deque, val)))
        return err;
    if (deque_size(&d->deque) == 1 && (err = pthread_cond_signal(&d->cond)))
        goto POP;
    if ((err = pthread_mutex_unlock(&d->lock)))
        goto POP;
    return OK;

POP:;
    type t;
    deque_pop_front(&d->deque, &t);
    return err;
}


int blocking_deque_push_back(blocking_deque_t *d, type * val) {
    int err;
    if ((err = pthread_mutex_lock(&d->lock)))
        return err;
    if ((err = deque_push_back(&d->deque, val)))
        return err;
    if (deque_size(&d->deque) == 1 && (err = pthread_cond_signal(&d->cond)))
        goto POP;
    if ((err = pthread_mutex_unlock(&d->lock)))
        goto POP;
    return OK;

POP:;
    type t;
    deque_pop_back(&d->deque, &t);
    return err;

}

int blocking_deque_pop_front(blocking_deque_t *d, type * val) {
    int err = 0;
    if ((err = pthread_mutex_lock(&d->lock)))
        return err;
    
    while (deque_is_empty(&d->deque) && err == 0) {
        err = pthread_cond_wait(&d->cond, &d->lock);
    }
    if (err) {
        pthread_mutex_unlock(&d->lock);
        return err;
    }
    assert(deque_pop_front(&d->deque, val) == 0);   // should not fail in any case

    pthread_mutex_unlock(&d->lock);

    return OK;
}

int blocking_deque_pop_back(blocking_deque_t *d, type * val) {
    int err = 0;
    if ((err = pthread_mutex_lock(&d->lock)))
        return err;
    
    while (deque_is_empty(&d->deque) && err == 0) {
        err = pthread_cond_wait(&d->cond, &d->lock);
    }
    if (err) {
        pthread_mutex_unlock(&d->lock);
        return err;
    }
    assert(deque_pop_back(&d->deque, val) == 0);   // should not fail in any case

    pthread_mutex_unlock(&d->lock);

    return OK;
}

#endif
