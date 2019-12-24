#ifndef DEQUE_H
#define DEQUE_H

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

#endif
