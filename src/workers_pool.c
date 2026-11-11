#include "worker.h"
#include "utilities.h"
#include "blocking_queue.h"
#include "dispatcher.h"
#include "workers_pool.h"
#include "logger.h"
#include "database.h"
#include "signal_shutdown.h"

Worker** workers;
int num_workers;

coro_sched_t *sched = NULL;

void worker_done_callback(Worker* worker, void* ctx) {
    WorkerPool* pool = (WorkerPool*)ctx;
    bq_enqueue(pool->workers_free, worker);
}


void assign_command(Worker* worker, RespObject* cmd) {
    cmd->client->is_executing=1;
    cor_mutex_lock(&worker->mutex);
    worker->command = cmd;
    worker->is_busy = true;
    cor_notify_one(worker->cond);
    cor_mutex_unlock(&worker->mutex);
}

void free_workers() {
    for (int i=0; i!=num_workers; i++) {
        free(workers[i]);
    }
    free(workers);
}

void* run_worker_pool(void* args) {
    WorkerPool* pool = args;
    while (!stop) {
        LOG(LOG_INFO, "test");
        RespObject* command = (RespObject*) get_command();
        if (command==NULL) {
            cor_sleep_ms(0);
            LOG(LOG_INFO, "trylock bad with command");
            continue;
        }
        else {
            Worker* worker = bq_dequeue(pool->workers_free);
            if (worker==NULL) {
                cor_sleep_ms(0);
                LOG(LOG_INFO, "trylock bad with worker");
                continue;
            }
            LOG(LOG_INFO, "size workers: %d", bq_get_size(pool->workers_free));
            assign_command(worker, command);
        }
        cor_yield();
    }
    for (int i=0; i!=num_workers; i++) {
        cor_notify_one(workers[i]->cond);
    }
    int tries = 0;
    bool flag=true;
    while (flag) {
        if (tries>100) {
            stop_coros();
        }
        tries++;
        flag = false;
        for (int i=0; i!=num_workers; i++) {
            if (!workers[i]->is_finished) {
                flag = true;
            }
        }
        LOG(LOG_INFO, "left workers");
        cor_sleep_ms(10);
    }
    workers_pool_shutdown=1;
    if (num_of_clients > 0) {
        for (Client* t = clients; t; t = t->next_client) {
            cor_notify_one(t->cond);
        }
    }
    tries=0;
    while(clients) {
        cor_sleep_ms(10);
        tries++;
        if (tries>100) {
            stop_coros();
        }
    }
    return nullptr;
}

void destroy_worker_pool(WorkerPool* wp) {
    free_workers();
    bq_destroy(wp->workers_free);
    free(wp->workers_free);
    free(wp);
}

void* run_scheduler(void* arg) {
    cor_run(sched);
    LOG(LOG_INFO, "shutdown worker_pool");
    return NULL;
}

WorkerPool* init_workers_pool(int workers_number, HashMap* database) {
    WorkerPool* pool = xmalloc(sizeof(WorkerPool));
    pool->workers_free = xmalloc(sizeof(BlockingQueue_t));
    bq_init(pool->workers_free, workers_number);
    sched = coro_sched_create((size_t)workers_number + 100, 32 * 1024 * 1024);

    num_workers=workers_number;
    workers = xmalloc(sizeof(Worker*)*workers_number);
    for (int i=0; i!=workers_number; i++) {
        Worker* worker = init_worker(i, worker_done_callback, pool, database, sched, 128 * 1024);
        workers[i] = worker;
        bq_enqueue(pool->workers_free, worker);
    }
    coro_create(sched, (coro_fn_t) run_worker_pool, pool, 128 * 1024);
    return pool;
}




