#include "blocking_queue.h"
#include "protocol_parser.h"
#include "utilities.h"
#include "dispatcher.h"
#include "signal_shutdown.h"

BlockingQueue_t* bq_commands;

#define QUEUE_CAPACITY 100000

void init_dispatcher(void) {
    bq_commands = xmalloc(sizeof(BlockingQueue_t));
    bq_init(bq_commands, QUEUE_CAPACITY);
}

void add_command(RespObject* resp_object) {
    if (workers_pool_shutdown) {
        free_resp_object(resp_object);
        return;
    }
    bq_enqueue(bq_commands, resp_object);
}

RespObject* get_command(void) {
    LOG(LOG_INFO, "dispatcher commands: %d", bq_get_size(bq_commands));
    return bq_dequeue(bq_commands);
}

void destroy_dispatcher(void) {
    RespObject* pending = NULL;
    while ((pending = bq_dequeue(bq_commands)) != NULL) {
        free_resp_object(pending);
    }

    bq_destroy(bq_commands);
    free(bq_commands);
}