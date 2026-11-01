#ifndef CODBOZ_PC_LOG_H
#define CODBOZ_PC_LOG_H

#include <stdio.h>

extern int g_log_verbose;

#define log_info(...)                                                                             \
    do {                                                                                           \
        fprintf(stderr, "[codboz] " __VA_ARGS__);                                                  \
        fputc('\n', stderr);                                                                       \
    } while (0)
#define log_error(...)                                                                            \
    do {                                                                                           \
        fprintf(stderr, "[codboz] error: " __VA_ARGS__);                                           \
        fputc('\n', stderr);                                                                       \
    } while (0)
#define log_debug(...)                                                                            \
    do {                                                                                           \
        if (g_log_verbose) {                                                                       \
            fprintf(stderr, "[codboz] " __VA_ARGS__);                                              \
            fputc('\n', stderr);                                                                   \
        }                                                                                          \
    } while (0)

#endif
