#include <sys/ucontext.h>
#include <ucontext.h>
#include <malloc.h>
#include <bits/time.h>
#include <time.h>
#include "coroutine_pool.h"
#include "utilities.h"

__thread coro_t *current_coro = nullptr;

typedef enum {
    CORO_READY,
    CORO_RUNNING,
    CORO_WAITING,
    CORO_FINISHED
} coro_state_t;

struct coro_t {
    ucontext_t ctx;
    coro_state_t state;
    coro_fn_t func;
    void *arg;
    char *stack;
    size_t stack_size;
    coro_t *next;
    coro_sched_t *sched;
    uint64_t id;
};

typedef struct coro_sched_t {
    coro_t **coros;
    size_t max_coroutines;
    size_t count;
    uint64_t next_id;
    ucontext_t main_ctx;
    bool stop_coros;
} coro_sched_t;


//обертка для флага завершения
void coro_wrapper(coro_t *c, void *arg) {
    current_coro = c;
    c->state = CORO_RUNNING;
    c->func(arg);
    c->state = CORO_FINISHED;
}

//создать шедулер
coro_sched_t *coro_sched_create(size_t max_coroutines, size_t stack_size) {
    coro_sched_t *sched = xmalloc(sizeof(coro_sched_t));
    if (!sched) return nullptr;

    sched->coros = xcalloc(max_coroutines, sizeof(coro_t *));

    sched->max_coroutines = max_coroutines;
    sched->count = 0;
    sched->next_id = 1;

    token_map_init(1024*10);
    return sched;
}

//удалить шедулер
void coro_sched_destroy(coro_sched_t *sched) {
    if (!sched) return;

    if (current_coro!=nullptr) return;

    for (size_t i = 0; i < sched->count; i++) {
        coro_t *c = sched->coros[i];
        if (!c) continue;

        if (c->stack) free(c->stack);
        free(c);
    }

    free(sched->coros);
    free(sched);
}

int coro_sched_init(coro_sched_t *sched, size_t max_coroutines, size_t stack_size) {
    if (!sched) return -1;

    sched->coros = xcalloc(max_coroutines, sizeof(coro_t *));

    sched->max_coroutines = max_coroutines;
    sched->count = 0;
    sched->next_id = 1;
    token_map_init(1024*10);
    return 0;
}

void coro_sched_shutdown(coro_sched_t *sched) {
    if (!sched) return;

    if (current_coro!=nullptr) return;

    for (size_t i = 0; i < sched->count; i++) {
        coro_t *c = sched->coros[i];
        if (!c) continue;

        if (c->stack) free(c->stack);
        free(c);
    }

    free(sched->coros);
    sched->coros = nullptr;
    sched->count = 0;
    sched->max_coroutines = 0;
    sched->next_id = 0;
}



//создать корутину
coro_t *coro_create(coro_sched_t *sched, coro_fn_t func, void *arg, size_t stack_size) {
    if (sched->count >= sched->max_coroutines) return nullptr;

    coro_t *c = xmalloc(sizeof(coro_t));


    c->func = func;
    c->arg = arg;
    c->state = CORO_READY;
    c->stack_size = stack_size;
    c->stack = xmalloc(stack_size);
    c->sched = sched;
    c->id = sched->next_id++;


    getcontext(&c->ctx);
    c->ctx.uc_stack.ss_sp = c->stack;
    c->ctx.uc_stack.ss_size = c->stack_size;
    c->ctx.uc_link = &sched->main_ctx;
    makecontext(&c->ctx, (void (*)())coro_wrapper, 2, c, arg);

    sched->coros[sched->count++] = c;
    return c;
}


//отдать управление шедулеру
void cor_yield(void) {
    if (!current_coro || current_coro->state==CORO_FINISHED) return;
    if (current_coro->state == CORO_RUNNING)
        current_coro->state = CORO_READY;
    swapcontext(&current_coro->ctx, &current_coro->sched->main_ctx);
}

//запустить шедулер
void cor_run(coro_sched_t *sched) {
    if (!sched) return;

    struct timespec ts = {0, 100 * 1000 * 1000};

    int active;
    do {
        if (sched->stop_coros) {
            break;
        }
        active = 0;
        for (size_t i = 0; i < sched->count; i++) {
            coro_t *c = sched->coros[i];
            if (c->state==CORO_READY) {
                current_coro = c;
                swapcontext(&sched->main_ctx, &c->ctx);
            }
            if (c->state!=CORO_FINISHED) {
                active = 1;
            }
        }
        cor_sleep_tick();
        int count_waiting=0;
        for (size_t i = 0; i < sched->count; i++) {
            coro_t *c = sched->coros[i];
            if (c->state==CORO_WAITING) {
                count_waiting++;
            }
        }
        //printf("count waiting %d sched, count shed %zu", count_waiting, sched->count);
        if (count_waiting==(sched->count-cor_count_finished(sched))) {
            nanosleep(&ts, nullptr);
        }
    } while (active);
    current_coro = nullptr;
}

//текущая корутина
coro_t *coro_current(void) {
    return current_coro;
}

//айди корутины
uint64_t coro_id(coro_t *c) {
    if (c!=nullptr) {
        return c->id;
    }
    return 0;
}

//выход из текущей корутины
void cor_exit(void) {
    coro_t *c = current_coro;
    c->state = CORO_FINISHED;
    setcontext(&c->sched->main_ctx);
    __builtin_unreachable();
}

//сколько корутин ready
size_t cor_count_ready(coro_sched_t *sched) {
    if (!sched) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < sched->count; i++)
        if (sched->coros[i]->state == CORO_READY)
            count++;
    return count;
}

//сколько корутин ждет
size_t cor_count_waiting(coro_sched_t *sched) {
    if (!sched) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < sched->count; i++)
        if (sched->coros[i]->state == CORO_WAITING)
            count++;
    return count;
}

//сколько корутин всего
size_t cor_count_total(coro_sched_t *sched) {
    if (!sched) {
        return 0;
    }
    return sched->count;
}

size_t cor_count_finished(coro_sched_t *sched) {
    if (!sched) {
        return 0;
    }

    size_t count = 0;
    for (size_t i = 0; i < sched->count; i++)
        if (sched->coros[i]->state == CORO_FINISHED)
            count++;
    return count;
}

//примитивы


//ОЧЕРЕДЬ
struct cor_queue_t {
    struct coro_t **items;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
};

cor_queue_t *cor_queue_create(size_t capacity) {
    cor_queue_t *q = xmalloc(sizeof(cor_queue_t));
    q->items = xmalloc(sizeof(struct coro_t *) * capacity);
    q->capacity = capacity;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    return q;
}

void cor_queue_destroy(cor_queue_t *q) {
    if (!q) return;
    free(q->items);
    free(q);
}

int cor_queue_push(cor_queue_t *q, struct coro_t *c) {
    if (!q || q->count >= q->capacity) {
        return -1;
    }
    q->items[q->tail] = c;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    return 0;
}

struct coro_t *cor_queue_pop(cor_queue_t *q) {
    if (!q || q->count == 0) {
        return nullptr;
    }
    struct coro_t *c = q->items[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    return c;
}

size_t cor_queue_size(cor_queue_t *q) {
    if (q==nullptr) {
        return 0;
    }
    return q->count;
}

//МЬЮТЕКС
void cor_mutex_init(cor_mutex_t *m) {
    m->locked = 0;
    m->owner = nullptr;
    m->waiters = cor_queue_create(64);
}

void cor_mutex_destroy(cor_mutex_t *m) {
    if (!m) {
        return;
    }
    cor_queue_destroy(m->waiters);
    m->waiters = nullptr;
    m->owner = nullptr;
    m->locked = 0;
}

void cor_mutex_lock(cor_mutex_t *m) {
    coro_t *c = coro_current();
    if (!c) {
        return;
    }

    while (m->locked&&m->owner->id!=c->id) {
        c->state = CORO_WAITING;
        cor_queue_push(m->waiters, c);
        cor_yield();
    }

    m->locked = 1;
    m->owner = c;
    c->state = CORO_RUNNING;
}

int cor_mutex_trylock(cor_mutex_t *m) {
    coro_t *c = coro_current();
    if (!c) {
        return -1;
    }

    if (m->locked == 0) {
        m->locked = 1;
        m->owner = c;
        return 0;
    }
    return -1;
}

void cor_mutex_unlock(cor_mutex_t *m) {
    coro_t *c = coro_current();
    if (!c || m->owner != c) {
        return;
    }

    if (cor_queue_size(m->waiters) > 0) {
        coro_t *next = cor_queue_pop(m->waiters);
        next->state = CORO_READY;
        m->owner = next;
    } else {
        m->locked = 0;
        m->owner = nullptr;
    }
}

void cor_sem_init(cor_semaphore_t *s, int initial) {
    s->value = initial;
    s->waiters = cor_queue_create(64);
}

void cor_sem_destroy(cor_semaphore_t *s) {
    if (!s) {
        return;
    }
    cor_queue_destroy(s->waiters);
    s->waiters = nullptr;
    s->value = 0;
}

void cor_sem_wait(cor_semaphore_t *s) {
    coro_t *c = coro_current();
    if (!c) {
        return;
    }

    while (s->value <= 0) {
        c->state = CORO_WAITING;
        cor_queue_push(s->waiters, c);
        cor_yield();
    }

    s->value--;
}

int cor_sem_trywait(cor_semaphore_t *s) {
    if (s->value > 0) {
        s->value--;
        return 0;
    }
    return -1;
}

void cor_sem_post(cor_semaphore_t *s) {
    s->value++;
    if (cor_queue_size(s->waiters) > 0) {
        coro_t *next = cor_queue_pop(s->waiters);
        next->state = CORO_READY;
    }
}

//РВЛОК

void cor_rwlock_init(cor_rwlock_t *rw) {
    rw->readers = 0;
    rw->writer = 0;
    rw->r_waiters = cor_queue_create(64);
    rw->w_waiters = cor_queue_create(64);
}

void cor_rwlock_destroy(cor_rwlock_t *rw) {
    if (!rw) {
        return;
    }
    cor_queue_destroy(rw->r_waiters);
    cor_queue_destroy(rw->w_waiters);
    rw->r_waiters = nullptr;
    rw->w_waiters = nullptr;
    rw->readers = 0;
    rw->writer = 0;
}

void cor_rwlock_rdlock(cor_rwlock_t *rw) {
    coro_t *c = coro_current();
    if (!c) {
        return;
    }

    while (rw->writer || cor_queue_size(rw->w_waiters) > 0) {
        c->state = CORO_WAITING;
        cor_queue_push(rw->r_waiters, c);
        cor_yield();
    }
    rw->readers++;
}

int cor_rwlock_tryrdlock(cor_rwlock_t *rw) {
    if (!rw) {
        return -1;
    }
    if (rw->writer || cor_queue_size(rw->w_waiters) > 0) {
        return -1;
    }
    rw->readers++;
    return 0;
}

void cor_rwlock_wrlock(cor_rwlock_t *rw) {
    coro_t *c = coro_current();
    if (!c) {
        return;
    }

    while (rw->writer || rw->readers) {
        c->state = CORO_WAITING;
        cor_queue_push(rw->w_waiters, c);
        cor_yield();
    }
    rw->writer = 1;
}


int cor_rwlock_trywrlock(cor_rwlock_t *rw) {
    if (!rw) {
        return -1;
    }
    if (rw->writer || rw->readers > 0) {
        return -1;
    }
    rw->writer = 1;
    return 0;
}

void cor_rwlock_unlock(cor_rwlock_t *rw) {
    if (rw->writer) {
        rw->writer = 0;
    } else if (rw->readers > 0) {
        rw->readers--;
    }

    if (rw->readers == 0) {
        if (cor_queue_size(rw->w_waiters) > 0) {
            coro_t *next = cor_queue_pop(rw->w_waiters);
            next->state = CORO_READY;
        } else {
            while (cor_queue_size(rw->r_waiters) > 0) {
                coro_t *next = cor_queue_pop(rw->r_waiters);
                next->state = CORO_READY;
            }
        }
    }
}

typedef struct sleep_entry_t {
    coro_t *coro;
    uint64_t wakeup_ms;
} sleep_entry_t;

#define MAX_SLEEPERS 128
sleep_entry_t sleepers[MAX_SLEEPERS];
size_t sleeper_count = 0;

uint64_t current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

int cor_sleep_ms(unsigned int ms) {
    coro_t *c = coro_current();
    if (!c) {
        return 1;
    }

    uint64_t now = current_time_ms();
    uint64_t wakeup = now + ms;

    if (sleeper_count >= MAX_SLEEPERS) {
        return 1;
    }

    c->state = CORO_WAITING;
    sleepers[sleeper_count++] = (sleep_entry_t){ .coro = c, .wakeup_ms = wakeup };

    cor_yield();

    now = current_time_ms();

    if (now>=wakeup) {
        return 1;
    }
    return 0;
}

void cor_sleep_tick(void) {
    uint64_t now = current_time_ms();
    for (size_t i = 0; i < sleeper_count; ) {
        if (sleepers[i].wakeup_ms <= now) {
            sleepers[i].coro->state = CORO_READY;
            sleepers[i] = sleepers[--sleeper_count];
        } else {
            i++;
        }
    }
}

//НОТИФАИ

typedef struct {
    cor_wait_token_t key;
    cor_queue_t *queue;
    int used;
} token_slot_t;

typedef struct {
    token_slot_t *slots;
    size_t cap;
    size_t count;
} token_map_t;

token_map_t token_map;

size_t token_hash(cor_wait_token_t k) {
    uintptr_t x = (uintptr_t)k;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (size_t)x;
}

void token_map_init(size_t cap) {
    if (token_map.slots==nullptr) {
        token_map.slots = calloc(cap, sizeof(token_slot_t));
        token_map.cap = cap;
        token_map.count = 0;
    }
}

token_slot_t *token_map_get_slot(cor_wait_token_t key) {
    size_t h = token_hash(key) % token_map.cap;
    while(true) {
        token_slot_t *s = &token_map.slots[h];
        if (!s->used) return nullptr;
        if (s->key == key) return s;
        h = (h + 1) % token_map.cap;
    }
}

token_slot_t *token_map_insert(cor_wait_token_t key) {
    size_t h = token_hash(key) % token_map.cap;
    while(true) {
        token_slot_t *s = &token_map.slots[h];
        if (!s->used) {
            s->used = 1;
            s->key = key;
            s->queue = cor_queue_create(64);
            token_map.count++;
            return s;
        }
        if (s->key == key) {
            return s;
        }
        h = (h + 1) % token_map.cap;
    }
}

void cor_wait_on(cor_wait_token_t token) {
    coro_t *c = coro_current();
    if (!c) {
        return;
    }
    token_slot_t *s = token_map_get_slot(token);
    if (!s) s = token_map_insert(token);
    c->state = CORO_WAITING;
    cor_queue_push(s->queue, c);
    cor_yield();
}

void cor_notify_one(cor_wait_token_t token) {
    token_slot_t *s = token_map_get_slot(token);
    if (!s) {
        return;
    }
    coro_t *c = cor_queue_pop(s->queue);
    if (c) c->state = CORO_READY;
}

void cor_notify_all(cor_wait_token_t token) {
    token_slot_t *s = token_map_get_slot(token);
    if (!s) {
        return;
    }
    while (cor_queue_size(s->queue) > 0) {
        coro_t *c = cor_queue_pop(s->queue);
        c->state = CORO_READY;
    }
}


int cor_cond_wait(cor_wait_token_t token, cor_mutex_t* mutex) {
    coro_t* c = coro_current();
    if (!c) return 1;

    token_slot_t* s = token_map_get_slot(token);
    if (!s) s = token_map_insert(token);

    c->state = CORO_WAITING;

    cor_queue_push(s->queue, c);

    cor_mutex_unlock(mutex);

    cor_yield();

    cor_mutex_lock(mutex);

    return 0;
}




//дебаг и статистика

const char *coro_state_name(coro_t *c) {
    if (!c) return "nullptr";
    switch (c->state) {
        case CORO_READY: return "READY";
        case CORO_RUNNING: return "RUNNING";
        case CORO_WAITING: return "WAITING";
        case CORO_FINISHED: return "FINISHED";
        default: return "UNKNOWN";
    }
}

void coro_dump_stats(coro_sched_t *sched) {
    if (!sched) return;

    size_t ready = 0;
    size_t running = 0;
    size_t waiting = 0;
    size_t finished = 0;

    for (size_t i = 0; i < sched->count; i++) {
        coro_t *c = sched->coros[i];
        if (!c) continue;
        switch (c->state) {
            case CORO_READY: ready++; break;
            case CORO_RUNNING: running++; break;
            case CORO_WAITING: waiting++; break;
            case CORO_FINISHED: finished++; break;
        }
    }

    fprintf(stderr, "Coroutines: total=%zu ready=%zu running=%zu waiting=%zu finished=%zu\n",
            sched->count, ready, running, waiting, finished);
}

#include "coroutine_pool.h"

void cor_rec_mutex_init(cor_rec_mutex_t *m) {
    m->owner = nullptr;
    m->count = 0;
    m->waiters = cor_queue_create(64);
}

void cor_rec_mutex_lock(cor_rec_mutex_t *m) {
    coro_t *self = coro_current();
    if (m->owner == self) {
        m->count++;
        return;
    }
    while (m->owner != nullptr) {
        cor_wait_on(m->waiters);
    }
    m->owner = self;
    m->count = 1;
}

int cor_rec_mutex_trylock(cor_rec_mutex_t *m) {
    coro_t *self = coro_current();
    if (m->owner == self) {
        m->count++;
        return 0;
    }
    if (m->owner == nullptr) {
        m->owner = self;
        m->count = 1;
        return 0;
    }
    return 1;
}

void cor_rec_mutex_unlock(cor_rec_mutex_t *m) {
    if (m->owner != coro_current()) return;
    m->count--;
    if (m->count == 0) {
        m->owner = nullptr;
        cor_notify_one(m->waiters);
    }
}

void cor_rec_mutex_destroy(cor_rec_mutex_t *m) {
    if (!m) return;
    if (m->waiters) {
        cor_queue_destroy(m->waiters);
        m->waiters = nullptr;
    }
    m->owner = nullptr;
    m->count = 0;
}

void stop_coros() {
    if (current_coro==NULL) return;
    current_coro->sched->stop_coros=true;
}