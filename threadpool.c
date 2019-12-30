#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include "threadpool.h"

static void deque_init(deque_t *d);
static void deque_destroy(deque_t *d);
static size_t deque_size(deque_t *d);
static int deque_is_empty(deque_t *d);
static int deque_push_front(deque_t *d, runnable_t * val);
static int deque_push_back(deque_t *d, runnable_t * val);
static int deque_pop_front(deque_t *d, runnable_t * val);
static int deque_pop_back(deque_t *d, runnable_t * val);

static int blocking_deque_init(blocking_deque_t *d);
static int blocking_deque_destroy(blocking_deque_t *d);
static size_t blocking_deque_size(blocking_deque_t *d);
static int blocking_deque_is_empty(blocking_deque_t *d);
static int blocking_deque_push_front(blocking_deque_t *d, runnable_t * val) ;
static int blocking_deque_push_back(blocking_deque_t *d, runnable_t * val);
static int blocking_deque_pop_front(blocking_deque_t *d, runnable_t * val);
static int blocking_deque_pop_back(blocking_deque_t *d, runnable_t * val);

static void* thread_worker(void* deque) {
    blocking_deque_t * tasks = deque;
    runnable_t runnable;

    sigset_t sigint_block;
    sigemptyset(&sigint_block);
    sigaddset(&sigint_block, SIGINT);
    pthread_sigmask(SIG_BLOCK, &sigint_block, NULL);

    while (1) {
        int err = blocking_deque_pop_front(tasks, &runnable);
        if (err) {
            return (void*)(ssize_t)err;
        }
        if (runnable.function == NULL) {
            return (void*)OK;
        }
        runnable.function(runnable.arg, runnable.argsz);
    }
}

static int create_threads(thread_pool_t * pool) {
    size_t i;
    for (i = 0; i < pool->pool_size; ++i) {
        if (pthread_create(&pool->threads[i], NULL, thread_worker, &pool->tasks)) {
            goto CLEANUP;
        }
    }
    return OK;

CLEANUP:
    while (i--) {
        // pthread_cond_wait is a cancellation point, therefore the threads
        // will be cancelled immediately
        assert(pthread_cancel(pool->threads[i]) == 0);
    }
    return ERR;
}

int thread_pool_init(thread_pool_t *pool, size_t num_threads) {
    pool->allow_adding = 1;
    pool->pool_size = num_threads;

    if (blocking_deque_init(&pool->tasks))
        goto DESTROY_NOTHING;

    pool->threads = calloc(num_threads, sizeof(*pool->threads));
    if (pool->threads == NULL)
        goto DESTROY_DEQUE;

    if (create_threads(pool))
        goto DESTROY_THREAD_ARRAY;

    return OK;

DESTROY_THREAD_ARRAY:
    free(pool->threads);
DESTROY_DEQUE:
    blocking_deque_destroy(&pool->tasks);
DESTROY_NOTHING:
    return ERR;
}

void thread_pool_destroy(struct thread_pool *pool) {
    for (size_t i = 0; i < pool->pool_size; ++i) {
        pthread_cancel(pool->threads[i]);
    }
    for (size_t i = 0; i < pool->pool_size; ++i) {
        pthread_join(pool->threads[i], NULL);
    }
    free(pool->threads);
    blocking_deque_destroy(&pool->tasks);
}

int defer(struct thread_pool *pool, runnable_t runnable) {
    if (blocking_deque_push_back(&pool->tasks, &runnable)) {
        return ERR;
    }
    return OK;
}

static void deque_init(deque_t *d) {
    d->size = 0;
    d->begin.prev = d->end.next = NULL;
    d->begin.next = &d->end;
    d->end.prev = &d->begin;
}

static void deque_destroy(deque_t *d) {
    for (node_t * ptr = d->begin.next; ptr != &d->end;) {
        node_t * next = ptr->next;
        free(ptr);
        ptr = next;
    }
}

static size_t deque_size(deque_t *d) {
    return d->size;
}

static int deque_is_empty(deque_t *d) {
    return deque_size(d) == 0;
}

static int deque_push_front(deque_t *d, runnable_t * val) {
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

static int deque_push_back(deque_t *d, runnable_t * val) {
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

static int deque_pop_front(deque_t *d, runnable_t * val) {
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


static int deque_pop_back(deque_t *d, runnable_t * val) {
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
    if ((err = pthread_mutexattr_init(attr)))
        return err;
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
    if ((err = _mutexattr_init(attr)))
        return err;
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

static int robust_mutex_unlock(pthread_mutex_t * mutex) {
    int err = 0;
    switch((err = pthread_mutex_lock(mutex))) {
      case EOWNERDEAD:
        err = pthread_mutex_consistent(mutex);
        if (err) return err;
      case 0:
        return 0;
      default:
        return err;
    }
}

static int blocking_deque_init(blocking_deque_t *d) {
    int err;
    memset(d, 0, sizeof(blocking_deque_t));
    if ((err = _mutex_init(&d->lock, &d->lock_attr)))
        return err;

    if ((err = pthread_cond_init(&d->cond, NULL))) {
        _mutex_destroy(&d->lock, &d->lock_attr);
        return err;
    }

    deque_init(&d->deque);
    return OK;
}

static int blocking_deque_destroy(blocking_deque_t *d) {
    int err;
    if ((err = robust_mutex_unlock(&d->lock))) {
        return err;
    }

    deque_destroy(&d->deque);
    pthread_cond_destroy(&d->cond);
    pthread_mutex_unlock(&d->lock);
    _mutex_destroy(&d->lock, &d->lock_attr);
}

static size_t blocking_deque_size(blocking_deque_t *d) {
    size_t ret;
    int err;
    if ((err = robust_mutex_unlock(&d->lock)))
        return err;
    ret = deque_size(&d->deque);
    pthread_mutex_unlock(&d->lock);
    return ret;
}

static int blocking_deque_is_empty(blocking_deque_t *d) {
    return blocking_deque_size(d) == 0;
}

static int blocking_deque_push_front(blocking_deque_t *d, runnable_t * val) {
    int err;
    if ((err = robust_mutex_unlock(&d->lock)))
        return err;
    if ((err = deque_push_front(&d->deque, val)))
        return err;
    if ((err = pthread_cond_signal(&d->cond)))
        goto POP;
    if ((err = pthread_mutex_unlock(&d->lock)))
        goto POP;
    return OK;

POP:;
    runnable_t t;
    deque_pop_front(&d->deque, &t);
    return err;
}


static int blocking_deque_push_back(blocking_deque_t *d, runnable_t * val) {
    int err;
    if ((err = robust_mutex_unlock(&d->lock)))
        return err;
    if ((err = deque_push_back(&d->deque, val)))
        return err;
    if ((err = pthread_cond_signal(&d->cond)))
        goto POP;
    if ((err = pthread_mutex_unlock(&d->lock)))
        goto POP;
    return OK;

POP:;
    runnable_t t;
    deque_pop_back(&d->deque, &t);
    return err;

}

static int blocking_deque_pop_front(blocking_deque_t *d, runnable_t * val) {
    int err;
    if ((err = robust_mutex_unlock(&d->lock)))
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

static int blocking_deque_pop_back(blocking_deque_t *d, runnable_t * val) {
    int err;
    if ((err = robust_mutex_unlock(&d->lock)))
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
