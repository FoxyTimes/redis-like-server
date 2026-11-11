#pragma once
#include "protocol_parser.h"
#include "database.h"
#include "coroutine_pool.h"

typedef struct Worker {
    int worker_id;
    bool is_busy;
    RespObject* command;
    HashMap* database;
    cor_mutex_t mutex;
    cor_wait_token_t cond;
    bool is_finished;

    void (*on_done)(struct Worker* worker, void* ctx);
    void* cb_ctx;
} Worker;

Worker* init_worker(int id, void (*on_done)(Worker*, void*), void* ctx, HashMap* database, coro_sched_t* sched, size_t stack_size);