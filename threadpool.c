#include <pthread.h>
#include <stdio.h>
#include <execinfo.h>
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
static int deque_push_back(deque_t *d, runnable_t * val);
static int deque_pop_front(deque_t *d, runnable_t * val);
static int deque_pop_back(deque_t *d, runnable_t * val);

static int blocking_deque_init(blocking_deque_t *d);
static int blocking_deque_destroy(blocking_deque_t *d);
static size_t blocking_deque_size(blocking_deque_t *d);
static int blocking_deque_push_back(blocking_deque_t *d, runnable_t * val);
static int blocking_deque_pop_front(blocking_deque_t *d, runnable_t * val);

static void* handler_thread(void*);
static void handle_sigint(__attribute__((unused)) int signo) {}

struct vector {
    thread_pool_t **arr;
    size_t size;
    size_t alloc_size;
    pthread_mutex_t lock;
    pthread_mutexattr_t lockattr;
};

pthread_t handler_tid;
static struct vector active_pools;

static int struct_vector_init(struct vector*);
static int struct_vector_push_back(struct vector*, thread_pool_t*);
static void struct_vector_destroy(struct vector*);
static void struct_vector_remove(struct vector* , thread_pool_t* );
int robust_mutex_lock(pthread_mutex_t *);

void fatal_error(int e) {
    if (e) {
        void *array[10];
        int size;
        size = backtrace(array, 10);
        fprintf(stderr, "Stumbled upon a fatal error.\n");
        if (errno) {
            fprintf(stderr, "errno: %s\n", strerror(errno));
        }
        int stderr_fileno = 2;
        backtrace_symbols_fd(array, size, stderr_fileno);
        abort();
    }
}

void FE(int) __attribute__((alias("fatal_error")));

__attribute__((constructor)) static void set_handlers() {
    struct sigaction act = {};
    act.sa_handler = handle_sigint;
    FE(sigaction(SIGINT, &act, NULL));

    sigset_t sigint_block;
    FE(sigemptyset(&sigint_block));
    FE(sigaddset(&sigint_block, SIGINT));
    FE(sigprocmask(SIG_UNBLOCK, &sigint_block, NULL));
    FE(struct_vector_init(&active_pools));
    FE(pthread_create(&handler_tid, NULL, handler_thread, NULL));
}

__attribute__((destructor)) void finish_work() {
    struct_vector_destroy(&active_pools);
    pthread_kill(handler_tid, SIGUSR1);
    pthread_join(handler_tid, NULL);
}


static void thread_pool_halt_threads(thread_pool_t* pool) {
    pool->allow_adding = 0;
    for (__typeof (pool->pool_size) i = 0; i < pool->pool_size; ++i) {
        runnable_t r = {};
        if (blocking_deque_push_back(&pool->tasks, &r))
            exit(EXIT_FAILURE);
    }
}

static void thread_pool_decomission_resources(thread_pool_t* pool) {
    pthread_t self = pthread_self();
    for (size_t i = 0; i < pool->pool_size; ++i) {
        if (!pthread_equal(self, pool->threads[i]))
            pthread_join(pool->threads[i], NULL);
    }
    sem_destroy(&pool->active_thread_counter);
    free(pool->threads);
    blocking_deque_destroy(&pool->tasks);
}

static void* handler_thread(__attribute__((unused)) void* arg) {
    sigset_t sigcatched, blocked;
    FE(sigemptyset(&sigcatched));
    FE(sigfillset(&blocked));
    FE(sigaddset(&sigcatched, SIGINT));
    FE(sigaddset(&sigcatched, SIGUSR1));
    FE(pthread_sigmask(SIG_SETMASK, &blocked, NULL));
    while (1) {
        fprintf(stderr, "%s", "waiting for signal");
        int sig_no;
    //    sigset_t oldmask;
      //  FE(pthread_sigmask(SIG_UNBLOCK, &sigint, &oldmask));
        FE(sigwait(&sigcatched, &sig_no));
        fprintf(stderr, "%s", "got signal");
     //   FE(pthread_sigmask(SIG_SETMASK, &oldmask, NULL));

        if (sig_no == SIGUSR1) {
            return NULL;
        } else if (sig_no != SIGINT) {
            FE(1);
        }
        FE(robust_mutex_lock(&active_pools.lock));
        for (size_t i = 0; i < active_pools.size; ++i) {
            if (active_pools.arr[i]) {
                thread_pool_halt_threads(active_pools.arr[i]);
            }
        }
        for (size_t i = 0; i < active_pools.size; ++i) {
            if (active_pools.arr[i]) {
                thread_pool_decomission_resources(active_pools.arr[i]);
                active_pools.arr[i] = NULL;
            }
        }
        active_pools.size = 0;
        pthread_mutex_unlock(&active_pools.lock);
    }
}


static void* thread_worker(void* p) {
    thread_pool_t* pool = p;
    blocking_deque_t * tasks = &pool->tasks;
    runnable_t runnable;

    while (1) {
        int err = blocking_deque_pop_front(tasks, &runnable);
        if (err) {
            sem_wait(&pool->active_thread_counter);
            return NULL;
        }
        if (runnable.function == NULL) {
            sem_wait(&pool->active_thread_counter);
            return NULL;
        }
        runnable.function(runnable.arg, runnable.argsz);
    }
}

static int create_threads(thread_pool_t * pool) {
    size_t i;
    for (i = 0; i < pool->pool_size; ++i) {
        if (pthread_create(&pool->threads[i], NULL, thread_worker, pool)) {
            goto CLEANUP;
        }
    }
    return OK;

CLEANUP:
    while (i--) {
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

    if (sem_init(&pool->active_thread_counter, 0, num_threads))
        goto DESTROY_THREAD_ARRAY;

    if (create_threads(pool))
        goto DESTROY_ACTIVE_THREAD_COUNTER;

    if (struct_vector_push_back(&active_pools, pool))
        goto DESTROY_ACTIVE_THREAD_COUNTER;

    return OK;

DESTROY_ACTIVE_THREAD_COUNTER:
    sem_destroy(&pool->active_thread_counter);
DESTROY_THREAD_ARRAY:
    free(pool->threads);
DESTROY_DEQUE:
    blocking_deque_destroy(&pool->tasks);
DESTROY_NOTHING:
    return ERR;
}

void thread_pool_destroy(struct thread_pool *pool) {
    thread_pool_halt_threads(pool);
    thread_pool_decomission_resources(pool);
    struct_vector_remove(&active_pools, pool);
}

int defer(struct thread_pool *pool, runnable_t runnable) {
    if (!pool->allow_adding)
        return ERR;
    return blocking_deque_push_back(&pool->tasks, &runnable);
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
    back->prev->next= &d->end;

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

int _mutex_init(pthread_mutex_t *mutex, pthread_mutexattr_t *attr) {
    int err;
    if ((err = _mutexattr_init(attr)))
        return err;
    if ((err = pthread_mutex_init(mutex, attr))) {
        pthread_mutexattr_destroy(attr);
        return err;
    }
    return OK;
}

void _mutex_destroy(pthread_mutex_t *mutex, pthread_mutexattr_t *attr) {
    pthread_mutex_destroy(mutex);
    pthread_mutexattr_destroy(attr);
}

int robust_mutex_lock(pthread_mutex_t * mutex) {
    int err = 0;
    switch((err = pthread_mutex_lock(mutex))) {
      case EOWNERDEAD:
        return pthread_mutex_consistent(mutex);
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

    if ((err = sem_init(&d->sem, 0, 0))) {
        _mutex_destroy(&d->lock, &d->lock_attr);
        return err;
    }

    deque_init(&d->deque);
    return OK;
}

static int blocking_deque_destroy(blocking_deque_t *d) {
    int err;
    if ((err = robust_mutex_lock(&d->lock))) {
        return err;
    }

    deque_destroy(&d->deque);
    sem_destroy(&d->sem);
    pthread_mutex_unlock(&d->lock);
    _mutex_destroy(&d->lock, &d->lock_attr);
    return OK;
}

static size_t blocking_deque_size(blocking_deque_t *d) {
    size_t ret;
    robust_mutex_lock(&d->lock);
    ret = deque_size(&d->deque);
    pthread_mutex_unlock(&d->lock);
    return ret;
}


static int blocking_deque_push_back(blocking_deque_t *d, runnable_t * val) {
    int err;
    if ((err = robust_mutex_lock(&d->lock)))
        return err;
    if ((err = deque_push_back(&d->deque, val)))
        return err;
    if ((err = pthread_mutex_unlock(&d->lock)))
        goto POP;
    if ((err = sem_post(&d->sem)))
        goto POP;
    return OK;

POP:;
    runnable_t t;
    deque_pop_back(&d->deque, &t);
    return err;
}

static int blocking_deque_pop_front(blocking_deque_t *d, runnable_t * val) {
    //int v;
    //sem_getvalue(&d->sem, &v);
    //printf("< pop  %ld: queue %p, sem %p, semv %d, r %p\n", pthread_self(), d, &d->sem, v, val);

    int err;

    while (sem_wait(&d->sem) == -1 && errno == EINTR);
    if (errno)
        return -1;

    //sem_getvalue(&d->sem, &v);
    //printf("> pop  %ld: queue %p, sem %p, semv %d, r %p\n", pthread_self(), d, &d->sem, v, val);

    if ((err = robust_mutex_lock(&d->lock)))
        return err;
    assert(deque_pop_front(&d->deque, val) == 0);   // should not fail in any case

    pthread_mutex_unlock(&d->lock);

    return OK;
}

static int struct_vector_init(struct vector* vec) {
    vec->size = 0;
    vec->alloc_size = 4;
    if ((vec->arr = calloc(vec->alloc_size, sizeof(thread_pool_t*))) == NULL) {
        return ERR;
    }
    if (_mutex_init(&vec->lock, &vec->lockattr)) {
        free(vec->arr);
        return ERR;
    }
    return OK;
}

static int struct_vector_push_back(struct vector* vec, thread_pool_t* pool) {
    FE(robust_mutex_lock(&vec->lock));
    for (size_t i = 0; i < vec->size; ++i) {
        if (vec->arr[i] == NULL) {
            vec->arr[i] = pool;
            pthread_mutex_unlock(&vec->lock);
            return OK;
        }
    }
    if (vec->size+1 > vec->alloc_size) {
        thread_pool_t** p = realloc(vec->arr, 2*vec->alloc_size);
        if (!p) {
            pthread_mutex_unlock(&vec->lock);
            return ERR;
        }
        vec->arr = p;
        vec->alloc_size *= 2;
    }
    vec->arr[vec->size] = pool;
    vec->size++;

    pthread_mutex_unlock(&vec->lock);
    return OK;
}

static void struct_vector_destroy(struct vector* vec) {
    free(vec->arr);
    _mutex_destroy(&vec->lock, &vec->lockattr);
}

static void struct_vector_remove(struct vector* vec, thread_pool_t* pool) {
    FE(robust_mutex_lock(&vec->lock));
    for (size_t i = 0; i < vec->size; ++i) {
        if (vec->arr[i] == pool) {
            vec->arr[i] = NULL;
            pthread_mutex_unlock(&vec->lock);
            return;
        }
    }
    pthread_mutex_unlock(&vec->lock);
}
