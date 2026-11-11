#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "logger.h"
#include "coroutine_pool.h"

typedef struct FreeBlock {
    size_t size;
    char* ptr;
    struct FreeBlock* next;
} FreeBlock;

typedef struct MemoryPool {
    uint8_t* pool;
    size_t size;
    FreeBlock* free_list;

    cor_rec_mutex_t lock;
    int running;
} MemoryPool;


MemoryPool* pool_create(size_t size);
void* pool_alloc(MemoryPool* mp, size_t size);
void pool_free(MemoryPool* mp, void* ptr, size_t size);
void pool_destroy(MemoryPool* mp);
char* pool_strdup(MemoryPool* mp, const char* s);
size_t count_free_space(MemoryPool* pool);