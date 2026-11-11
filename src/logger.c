#include <stdarg.h>
#include <limits.h>
#include <string.h>
#include "stdio.h"
#include <unistd.h>
#include "include/logger.h"
#include "include/my_time.h"
#include "coroutine_pool.h"

static cor_mutex_t logger_mutex;

bool is_logger_init = false;
char path_to_file[PATH_MAX+1] = {0};
int logger_file_size_limit = 0;
bool is_debug = false;

void set_debug(bool debug) {
    is_debug = debug;
}

int init_logger(const char* path, int file_size_limit) {
    if (is_logger_init || file_size_limit < -1) {
        return 1;
    }
    cor_mutex_init(&logger_mutex);

    if (path == NULL) {
        is_logger_init=true;
        return 0;
    }
    if (strlen(path) >= PATH_MAX) {
        LOG(LOG_ERROR, "Path to file is too long, run default\n");
        is_logger_init=true;
        return 0;
    }

    strncpy(path_to_file, path, PATH_MAX);
    path_to_file[PATH_MAX] = '\0';

    is_logger_init = true;
    logger_file_size_limit = file_size_limit;
    return 0;
}

int fini_logger(void) {
    if(!is_logger_init) {
        return 1;
    }
    is_logger_init = false;
    return 0;
}

int write_log(enum OutputStream stream, enum LogLevel level, char* filename, int line_number, char* format, va_list args) {
    if (is_logger_init) {
        // поток записи логов
        if (level == LOG_DEBUG && !is_debug) {
            return 0;
        }
        FILE* file = NULL;
        switch (stream) {
            case STDOUT:
                file = stdout;
                break;
            case STDERR:
                file = stderr;
                break;
            case FILESTREAM:
                file = fopen(path_to_file, "a");
                if (file == NULL) {
                    fprintf(stderr, "Failed to open log file: %s\n", filename);
                    return 1;
                }
                break;
            default:
                fprintf(stderr, "Invalid output stream\n");
                return 1;
        }

        //проверка на размер файла (в КБ)
        if (stream==FILESTREAM&&logger_file_size_limit>=0) {
            fseek(file, 0, SEEK_END);
            long file_size = ftell(file);
            printf("%ld", file_size);
            if (file_size > logger_file_size_limit * 1024) {
                fclose(file);
                file = fopen(path_to_file, "w");
                if (file == NULL) {
                    fprintf(stderr, "Failed to open log file: %s\n", filename);
                    return 1;
                }
            }
        }

        //запись времени
        time_t t = get_time();
        struct tm *tm_info = localtime(&t);
        if (!tm_info) {
            fprintf(stderr, "Failed to get local time\n");
            return 1;
        }

        char tz_buffer[32];
        strftime(tz_buffer, sizeof(tz_buffer), "%Z", tm_info);


        if (tz_buffer[0] == '\0') {
            fprintf(stderr, "Failed to get timezone\n");
            return 1;
        }

        pid_t pid = getpid();
        if (pid == -1) {
            fprintf(stderr, "Failed to get process ID\n");
            return 1;
        }

        //уровни логирования
        switch (level) {
            case LOG_DEBUG:
                fprintf(file, "DEBUG: ");
                break;
            case LOG_INFO:
                fprintf(file, "INFO: ");
                break;
            case LOG_WARNING:
                fprintf(file, "WARNING: ");
                break;
            case LOG_ERROR:
                fprintf(file, "ERROR: ");
                break;
            case LOG_FATAL:
                fprintf(file, "FATAL: ");
                break;
            default:
                printf("Invalid log level\n");
                return 1;
        }

        fprintf(file, "%d-%d-%dT%d:%d:%d(%s) ",
                localtime(&t)->tm_year + 1900,
                localtime(&t)->tm_mon + 1,
                localtime(&t)->tm_mday,
                localtime(&t)->tm_hour,
                localtime(&t)->tm_min,
                localtime(&t)->tm_sec,
                tz_buffer);
        fprintf(file, "%s:", filename);
        fprintf(file, "%d ", line_number);
        fprintf(file, "[%d] | ", pid);

        vfprintf(file, format, args);
        fprintf(file, "\n");

        if (stream == FILESTREAM) {
            fclose(file);
        }
    } else {
        return 1;
    }
}

int logg(enum LogLevel level, char* filename, int line_number, char* format, ...) {
    va_list args;
    int result;


    va_start(args, format);

    if (is_debug) {
        result = write_log(STDOUT, level, filename, line_number, format, args);
    }
    else if (path_to_file[0] != 0) {
        result = write_log(FILESTREAM, level, filename, line_number, format, args);
    }
    else {
        result = write_log(STDERR, level, filename, line_number, format, args);
    }

    va_end(args);


    return result;
}


