#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "listener.h"
#include "types.h"



ParseResult parse_resp_stream(char *data, size_t len);
void print_resp(RespObject *o, int indent);
void add_callback(RespObject* command, void (*on_complete)(void* arg));
void free_resp(RespObject *o);
void free_parse_result(ParseResult *r);