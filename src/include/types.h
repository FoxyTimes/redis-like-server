#pragma once

#include <stdatomic.h>
#include "blocking_queue.h"

typedef struct Client Client;
typedef struct RespObject RespObject;
typedef struct Response Response;
typedef struct RespReader RespReader;
typedef struct ParseResult ParseResult;




typedef struct Entry {
    char* key;
    char* value;
    struct Entry* next;

    struct Entry* next_in_order;
    struct Entry* prev_in_order;
    struct Entry* end_of_order;
} Entry;



typedef struct Client {
    int client_fd;
    BlockingQueue_t* client_commands;
    atomic_int is_first_command;
    int event_fd;
    cor_mutex_t mutex;
    cor_wait_token_t cond;

    atomic_int is_deleting;
    atomic_int is_executing;

    char buffer[4096];
    size_t buffer_len;

    Client* next_client;
    Client* prev_client;

    atomic_int is_joined;
} Client;


typedef struct Response {
    char* answer;
    Client* client;
} Response;

typedef enum RespType {
    RESP_SIMPLE_STRING,
    RESP_ERROR,
    RESP_INTEGER,
    RESP_BULK_STRING,
    RESP_ARRAY,
    RESP_NULL
} RespType;

typedef struct RespObject {
    RespType type;
    char *str;                   // для строк
    long long integer;           // для чисел
    int elements;                // для массивов
    struct RespObject **items;   // вложенные объекты
    void (*on_complete)(void* arg);
    void* ctx;
    Client* client;
} RespObject;

typedef struct RespReader {
    const char *data;
    size_t pos;
    size_t len;
} RespReader;

typedef struct ParseResult {
    RespObject **cmds;   // массив готовых команд
    size_t count;        // сколько готовых
    size_t used_bytes;   // сколько байт из буфера реально было разобрано
} ParseResult;


void free_resp_object(RespObject* obj);
void free_client(Client* client);