#include <stdio.h>
#include <sys/eventfd.h>
#include <errno.h>
#include "include/logger.h"
#include "listener.h"
#include "dispatcher.h"
#include "workers_pool.h"
#include "database.h"
#include "memory_pool.h"
#include "config.h"
#include "signal_shutdown.h"
#include "coroutine_pool.h"

// Поток-функция для listener
void* listener_thread(void* arg) {
    int port = *(int*)arg;
    start_listener(port);
    return NULL;
}

int main(int argc, char *argv[]) {
    init_logger("", -1);
    listener_efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (listener_efd == -1) {
        LOG(LOG_FATAL, "eventfd failed: %s", strerror(errno));
        exit(1);
    }

    struct sigaction sa = {nullptr};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    sa.sa_handler = SIG_IGN;
    for (int sig = 1; sig < NSIG; sig++) {
        if (sig == SIGINT || sig == SIGTERM || sig == SIGKILL || sig == SIGSTOP)
            continue;
        sigaction(sig, &sa, nullptr);
    }


    create_config_table();
    parse_config("repa.conf");

    init_dispatcher();

    MemoryPool* mp = pool_create(get_variable("pool_size").data.integer[1]);
    HashMap* map = hashmap_create(mp,
                                  (int)get_variable("table_size").data.integer[1],
                                  (int)get_variable("num_locks").data.integer[1]);

    WorkerPool* wp = init_workers_pool((int)get_variable("num_workers").data.integer[1], map);

    LOG(LOG_INFO, "worker pool started");

    int port = 6379;
    coro_create(sched, (coro_fn_t) listener_thread, &port, 128 * 1024);

    LOG(LOG_INFO, "test");

    run_scheduler(wp);


    destroy_worker_pool(wp);
    destroy_dispatcher();
    hashmap_destroy(map);
    pool_destroy(mp);

    return 0;
}
