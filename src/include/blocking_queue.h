#pragma once
#include <stdatomic.h>
#include "coroutine_pool.h"


typedef struct BlockingQueue_t {
    void **data;
    int capacity;
    atomic_int size;
    int head;
    int tail;
    cor_mutex_t mutex;
    cor_wait_token_t not_full;
    cor_wait_token_t not_empty;
} BlockingQueue_t;

void bq_init(BlockingQueue_t *q, int capacity);
void bq_destroy(BlockingQueue_t *q);
void bq_enqueue(BlockingQueue_t *q, void* value);
void* bq_dequeue(BlockingQueue_t *q);
int bq_get_size(BlockingQueue_t *q);
//void bq_print(BlockingQueue_t *q);
void* bq_try_dequeue(BlockingQueue_t *q);