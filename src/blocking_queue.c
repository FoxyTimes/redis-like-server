#include <stdlib.h>
#include <stdatomic.h>
#include "blocking_queue.h"
#include "utilities.h"
#include "signal_shutdown.h"

void bq_init(BlockingQueue_t *q, int capacity) {
    q->data = xmalloc(sizeof(void*) * capacity);
    q->capacity = capacity;
    atomic_init(&q->size, 0);
    q->head = 0;
    q->tail = 0;
    q->not_full = (cor_wait_token_t)xmalloc(1);
    q->not_empty = (cor_wait_token_t)xmalloc(1);
    cor_mutex_init(&q->mutex);
}

void bq_destroy(BlockingQueue_t *q) {
    free(q->data);
    cor_mutex_destroy(&q->mutex);
}

void bq_enqueue(BlockingQueue_t *q, void* value) {
    cor_mutex_lock(&q->mutex);

    while (atomic_load(&q->size) == q->capacity && !stop) {
        cor_cond_wait(q->not_full, &q->mutex);
    }

    if (stop) {
        cor_mutex_unlock(&q->mutex);
        return;
    }

    q->data[q->tail] = value;
    q->tail = (q->tail + 1) % q->capacity;
    atomic_fetch_add(&q->size, 1);

    cor_notify_one(q->not_empty);
    cor_mutex_unlock(&q->mutex);
}

void* bq_dequeue(BlockingQueue_t *q) {
    cor_mutex_lock(&q->mutex);

    while (atomic_load(&q->size) == 0 && !stop) {
        cor_cond_wait(q->not_empty, &q->mutex);
    }

    if (stop) {
        cor_mutex_unlock(&q->mutex);
        return NULL;
    }

    void* value = q->data[q->head];
    q->head = (q->head + 1) % q->capacity;
    atomic_fetch_sub(&q->size, 1);

    cor_notify_one(q->not_full);
    cor_mutex_unlock(&q->mutex);

    return value;
}

int bq_get_size(BlockingQueue_t *q) {
    return atomic_load(&q->size);
}

/*void bq_print(BlockingQueue_t *q) {
    pthread_mutex_lock(&q->mutex);

    int size = atomic_load(&q->size);
    printf("BlockingQueue [size=%d, capacity=%d]:\n", size, q->capacity);

    if (size == 0) {
        printf("  <empty>\n");
        pthread_mutex_unlock(&q->mutex);
        return;
    }

    int idx = q->head;
    for (int i = 0; i < size; i++) {
        Entry* e = (Entry*)q->data[idx];
        if (e != NULL) {
            printf("  [%d] key=\"%s\", value=\"%s\"\n",
                   i,
                   e->key ? e->key : "(null)",
                   e->value ? e->value : "(null)");
        } else {
            printf("  [%d] (null entry)\n", i);
        }
        idx = (idx + 1) % q->capacity;
    }

    pthread_mutex_unlock(&q->mutex);
}*/

void* bq_try_dequeue(BlockingQueue_t *q) {
    if (cor_mutex_trylock(&q->mutex) != 0) {
        // mutex занят — не блокируемся
        return NULL;
    }

    if (atomic_load(&q->size) == 0) {
        // очередь пуста
        cor_mutex_unlock(&q->mutex);
        return NULL;
    }

    void* value = q->data[q->head];
    q->head = (q->head + 1) % q->capacity;
    atomic_fetch_sub(&q->size, 1);

    cor_notify_one(q->not_full);
    cor_mutex_unlock(&q->mutex);

    return value;
}

