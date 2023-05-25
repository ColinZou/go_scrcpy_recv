#include <stdarg.h>
#include <stdio.h>
#include <logging.h>

void debug_logf_actual(int line, char *file, char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  printf("%s#%d ", file, line);
  vprintf(fmt, args);
}
