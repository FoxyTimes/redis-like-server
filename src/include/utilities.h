#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

void *xmalloc(size_t size);
void *xrealloc(void* ptr, size_t size);
void *xcalloc(size_t size, int n);