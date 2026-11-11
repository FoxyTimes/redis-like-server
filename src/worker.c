#include "worker.h"
#include "utilities.h"
#include "logger.h"
#include <unistd.h>
#include "database.h"
#include "signal_shutdown.h"
#include "coroutine_pool.h"

void execute_command(RespObject* command, HashMap* database) {
    LOG(LOG_INFO, "START EXECUTING");
    process_the_command(command, database);
    LOG(LOG_INFO, "FINISH EXECUTING");
    free_resp_object(command);
}

void* run(void* args) {
    Worker* self = (Worker*) args;
    while (!stop) {
        cor_mutex_lock(&self->mutex);

        if (!self->is_busy) {
            LOG(LOG_INFO, "worker with id %d cond_wait", self->worker_id);
            cor_cond_wait(self->cond, &self->mutex);
            LOG(LOG_INFO, "worker with id %d stop cond_wait", self->worker_id);
        }
        if (stop) {
            cor_mutex_unlock(&self->mutex);
            break;
        }

        execute_command(self->command, self->database);
        self->is_busy = false;

        self->on_done(self, self->cb_ctx);

        cor_mutex_unlock(&self->mutex);
        LOG(LOG_INFO, "work is done: %d", self->worker_id);
        cor_yield();
    }
    LOG(LOG_INFO, "worker with id %d exit", self->worker_id);
    self->is_finished=true;
    cor_exit();
}


Worker* init_worker(int id, void (*on_done)(Worker*, void*), void* ctx, HashMap* database, coro_sched_t* sched, size_t stack_size) {
    Worker* worker = xmalloc(sizeof(Worker));
    worker->worker_id=id;
    worker->is_busy = false;
    worker->on_done = on_done;
    worker->cb_ctx = ctx;
    worker->database = database;
    worker->cond = (cor_wait_token_t)xmalloc(1);

    cor_mutex_init(&worker->mutex);

    coro_create(sched, (coro_fn_t) run, worker, stack_size);

    LOG(LOG_DEBUG, "worker with id: %d created", id);
    return worker;
}
