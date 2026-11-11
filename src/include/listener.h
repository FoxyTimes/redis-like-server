#pragma once

#include "blocking_queue.h"
#include <stdbool.h>
#include "logger.h"
#include "utilities.h"
#include "types.h"
#include "protocol_parser.h"
#include "dispatcher.h"

extern cor_mutex_t clients_lock;
extern Client* clients;
extern int num_of_clients;
void start_listener(int port);
void* client_thread(void * arg);
