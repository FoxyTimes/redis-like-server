#pragma once
#include <stdarg.h>
#include <stdbool.h>

#define LOG(level, format, ...) logg(level, __FILE__, __LINE__, format, ##__VA_ARGS__)

enum OutputStream {
    STDOUT = 1,
    STDERR,
    FILESTREAM
};

enum LogLevel {
    LOG_DEBUG = 1,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_FATAL,
};

void set_debug(bool debug);
int init_logger(const char* path, int file_size_limit);
int fini_logger(void);
int write_log(enum OutputStream stream, enum LogLevel level, char* filename, int line_number, char* format, va_list args);
int logg(enum LogLevel level, char* filename, int line_number, char* format, ...);
