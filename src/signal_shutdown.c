#define _GNU_SOURCE
#include <signal_shutdown.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include "logger.h"
#include "utilities.h"

atomic_int stop = 0;
atomic_int workers_pool_shutdown = 0;
int listener_efd = -1;

atomic_int threads_not_completed=0;
atomic_int all_free = 0;

void handle_signal(int sig) {
    stop = 1;
    if (listener_efd != -1) {
        uint64_t one = 1;
        write(listener_efd, &one, sizeof(one));
    }
}



void set_timeout_kill(pthread_t* tids, int num_tids, int time) {
    threads_not_completed++;

    pthread_t* tids_copy = xmalloc(sizeof(pthread_t) * num_tids);
    for (int i = 0; i < num_tids; i++) {
        tids_copy[i] = tids[i];
    }

    void* ret;

    int* finished = calloc(num_tids, sizeof(int));
    if (!finished) {
        perror("calloc");
        _exit(1);
    }
    for (int sec = 1; sec <= time; sec++) {
        int all_finished = 1;

        for (int i = 0; i < num_tids; i++) {
            if (finished[i]) continue;

            int rc = pthread_tryjoin_np(tids[i], &ret);
            if (rc == 0) {
                LOG(LOG_INFO, "finish %lu in timeout_kill_wait", tids[i]);
                finished[i] = 1;
            } else if (sec == time) {
                LOG(LOG_INFO, "tid %lu killed by timeout_kill_wait", tids[i]);
                pthread_cancel(tids[i]);
                pthread_tryjoin_np(tids[i], &ret);
                finished[i] = 1;
            }

            if (!finished[i]) all_finished = 0;
        }

        if (all_finished)
            break;

        sleep(1);
    }
    free(finished);
    free(tids_copy);

    threads_not_completed--;
}

