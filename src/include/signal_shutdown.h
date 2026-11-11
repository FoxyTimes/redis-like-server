#pragma once
#include "signal.h"
#include <stdatomic.h>

extern atomic_int stop;
extern atomic_int workers_pool_shutdown;
extern atomic_int listener_shutdown;
extern int listener_efd;

extern atomic_int threads_not_completed;

void handle_signal(int sig);
//void set_timeout_kill(pthread_t* tids, int num_tids, int time);