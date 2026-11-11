#include "memory_pool.h"
#include "utilities.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "logger.h"

void pool_defrag(MemoryPool* mp) {
    if (!mp) return;

    FreeBlock* curr = mp->free_list;
    while (curr && curr->next) {
        if (curr->ptr + curr->size == curr->next->ptr) {
            FreeBlock* tmp = curr->next;
            curr->size += tmp->size;
            curr->next = tmp->next;
            free(tmp);
        } else {
            curr = curr->next;
        }
    }
}

MemoryPool* pool_create(size_t size) {
    MemoryPool* mp = xmalloc(sizeof(MemoryPool));
    if (!mp) return nullptr;

    mp->pool = xmalloc(size);
    if (!mp->pool) {
        free(mp);
        return nullptr;
    }

    mp->size = size;

    mp->free_list = xmalloc(sizeof(FreeBlock));
    if (!mp->free_list) {
        free(mp->pool);
        free(mp);
        return nullptr;
    }

    mp->free_list->ptr = mp->pool;
    mp->free_list->size = size;
    mp->free_list->next = nullptr;

    // === создаём рекурсивный мьютекс ===
    cor_rec_mutex_init(&mp->lock);
    // ===================================

    mp->running = 1;

    return mp;
}

void* pool_alloc(MemoryPool* mp, size_t size) {
    if (!mp || size == 0) return NULL;

    cor_rec_mutex_lock(&mp->lock);
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    FreeBlock** prev = &mp->free_list;
    FreeBlock* curr = mp->free_list;

    while (curr) {
        if (curr->size >= size) {
            void* ptr = curr->ptr;

            if (curr->size > size) {
                curr->ptr += size;
                curr->size -= size;
            } else {
                *prev = curr->next;
                free(curr);
            }

            LOG(LOG_INFO, "MemoryPool free list after alloc():");
            curr = mp->free_list;
            while (curr) {
                LOG(LOG_INFO, "  block at %p, size = %zu", (void*)curr->ptr, curr->size);
                curr = curr->next;
            }

            cor_rec_mutex_unlock(&mp->lock);
            return ptr;
        }

        prev = &curr->next;
        curr = curr->next;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec)
                     + (end.tv_nsec - start.tv_nsec) / 1e9;
    LOG(LOG_WARNING, "%.6f секунд", elapsed);
    cor_rec_mutex_unlock(&mp->lock);
    LOG(LOG_ERROR, "Out of memory in the pool");
    return NULL;
}

void pool_free(MemoryPool* mp, void* ptr, size_t size) {
    if (!mp || !ptr || size == 0) return;

    FreeBlock* block = xmalloc(sizeof(FreeBlock));
    if (!block) {
        LOG(LOG_ERROR, "Failed to allocate FreeBlock structure");
        return;
    }

    block->ptr = (char*)ptr;
    block->size = size;
    block->next = nullptr;

    cor_rec_mutex_lock(&mp->lock);

    FreeBlock** prev = &mp->free_list;
    FreeBlock* curr = mp->free_list;
    while (curr && curr->ptr < block->ptr) {
        prev = &curr->next;
        curr = curr->next;
    }

    block->next = curr;
    *prev = block;

    pool_defrag(mp);

    LOG(LOG_INFO, "MemoryPool free list after free():");
    curr = mp->free_list;
    while (curr) {
        LOG(LOG_INFO, "  block at %p, size = %zu", (void*)curr->ptr, curr->size);
        curr = curr->next;
    }

    cor_rec_mutex_unlock(&mp->lock);
}

void pool_destroy(MemoryPool* mp) {
    if (!mp) return;

    cor_rec_mutex_lock(&mp->lock);
    mp->running = 0;

    FreeBlock* block = mp->free_list;
    while (block) {
        FreeBlock* next = block->next;
        free(block);
        block = next;
    }
    mp->free_list = nullptr;

    // освобождаем сам пул
    if (mp->pool) {
        free(mp->pool);
        mp->pool = nullptr;
    }

    mp->size = 0;

    cor_rec_mutex_unlock(&mp->lock);
    cor_rec_mutex_destroy(&mp->lock);

    free(mp);
}

char* pool_strdup(MemoryPool* mp, const char* s) {
    if (!mp || !s) return nullptr;

    cor_rec_mutex_lock(&mp->lock);  // можно безопасно блокировать, т.к. рекурсивный
    size_t len = strlen(s) + 1;
    char* copy = (char*)pool_alloc(mp, len);
    if (copy)
        memcpy(copy, s, len);
    cor_rec_mutex_unlock(&mp->lock);

    return copy;
}

size_t count_free_space(MemoryPool* pool) {
    size_t result = 0;
    FreeBlock* temp = pool->free_list;
    while (temp) {
        result += temp->size;
        temp=temp->next;
    }
    return result;
}
