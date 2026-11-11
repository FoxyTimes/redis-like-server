#include "include/my_time.h"
#include <sys/time.h>
#include "stdio.h"

time_t get_time() {
    time_t t;
    time(&t);
    return t;
}

long long get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}