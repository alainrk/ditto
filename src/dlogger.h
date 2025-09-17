#ifndef DLOGGER_H
#define DLOGGER_H

#include <stdio.h>

#define DLOG_LEVEL_ERROR 1
#define DLOG_LEVEL_WARN 2
#define DLOG_LEVEL_INFO 3
#define DLOG_LEVEL_DEBUG 4
#define DLOG_LEVEL_TRACE 5

typedef struct {
  FILE *f;
  int level;
} DLogger;

/**
 * Create a new DLogger instance that uses stdout
 */
DLogger *dlog_init(int level);

/**
 * Create a new DLogger instance that uses the specified file
 * and log level.
 */
DLogger *dlog_initf(FILE *f, int level);

void dlog_error(DLogger *l, const char *format, ...);
void dlog_warn(DLogger *l, const char *format, ...);
void dlog_info(DLogger *l, const char *format, ...);
void dlog_debug(DLogger *l, const char *format, ...);
void dlog_trace(DLogger *l, const char *format, ...);

/**
 * Close the DLogger instance and free the memory.
 */
void dlog_close(DLogger *l);

#endif
