#include "dlogger.h"
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

DLogger *dlog_initf(FILE *f, int level) {
  DLogger *dlog = malloc(sizeof(DLogger));

  dlog->f = f;
  dlog->level = level;

  return dlog;
}

DLogger *dlog_init(int level) { return dlog_initf(stdout, level); }

void _log(DLogger *l, const char *format, va_list args) {
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  char timestr[64];
  strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", tm);

  fprintf(l->f, "%s - ", timestr);
  vfprintf(l->f, format, args);
  fprintf(l->f, "\n");
  fflush(l->f);
}

void dlog_error(DLogger *dlog, const char *format, ...) {
  if (dlog->level >= DLOG_LEVEL_ERROR) {
    va_list args;
    va_start(args, format);
    _log(dlog, format, args);
    va_end(args);
  }
}

void dlog_warn(DLogger *dlog, const char *format, ...) {
  if (dlog->level >= DLOG_LEVEL_WARN) {
    va_list args;
    va_start(args, format);
    _log(dlog, format, args);
    va_end(args);
  }
}

void dlog_info(DLogger *dlog, const char *format, ...) {
  if (dlog->level >= DLOG_LEVEL_INFO) {
    va_list args;
    va_start(args, format);
    _log(dlog, format, args);
    va_end(args);
  }
}

void dlog_debug(DLogger *dlog, const char *format, ...) {
  if (dlog->level >= DLOG_LEVEL_DEBUG) {
    va_list args;
    va_start(args, format);
    _log(dlog, format, args);
    va_end(args);
  }
}

void dlog_trace(DLogger *dlog, const char *format, ...) {
  if (dlog->level >= DLOG_LEVEL_TRACE) {
    va_list args;
    va_start(args, format);
    _log(dlog, format, args);
    va_end(args);
  }
}

void dlog_close(DLogger *dlog) {
  fclose(dlog->f);
  free(dlog);
}
