#include "utilities.h"
#include "logger.h"

void *xmalloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        LOG(LOG_FATAL, "%s", "memory allocation error");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *xrealloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        LOG(LOG_FATAL, "%s", "memory reallocation error");
        exit(EXIT_FAILURE);
    }
    return new_ptr;
}

void *xcalloc(size_t size, int n) {
    void *ptr = calloc(n, size);
    if (!ptr) {
        LOG(LOG_FATAL, "%s", "memory allocation error");
        exit(EXIT_FAILURE);
    }
    return ptr;
}