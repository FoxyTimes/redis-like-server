#pragma once

#include "blocking_queue.h"
#include "database.h"
#include "coroutine_pool.h"

extern coro_sched_t* sched;

typedef struct WorkerPool {
    BlockingQueue_t* workers_free;
} WorkerPool;

WorkerPool* init_workers_pool(int workers_number, HashMap* database);
void destroy_worker_pool(WorkerPool* wp);
void* run_scheduler(void* arg);