#pragma once
#include "protocol_parser.h"
#include "listener.h"

extern BlockingQueue_t* bq_commands;

void init_dispatcher(void);
void add_command(RespObject* command);
RespObject* get_command(void);
void destroy_dispatcher(void);
