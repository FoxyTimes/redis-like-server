#pragma once
#include <stdatomic.h>
#include "memory_pool.h"
#include "types.h"
#include "coroutine_pool.h"

typedef struct HashMap {
    Entry** table;
    int table_size;
    atomic_size_t size;
    MemoryPool* pool;
    int num_locks;
    cor_rwlock_t* locks;
    cor_rwlock_t rwlock;
    Entry* order;

    //stats
    atomic_size_t total_commands_processed;
    atomic_size_t cmd_get;
    atomic_size_t cmd_set;
    atomic_size_t cmd_del;

} HashMap;

void process_the_command(RespObject* command, HashMap* map);
HashMap* hashmap_create(MemoryPool* mp, int table_size, int num_locks);
void hashmap_destroy(HashMap* map);
int hashmap_free(HashMap* map, Entry* e);
int hashmap_del(HashMap *map, const char *key);