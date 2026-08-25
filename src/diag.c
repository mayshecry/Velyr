#include "velyr.h"

void diagnostic(Diagnostics * d, int line, int col,
  const char * kind,
    const char * msg) {
  fprintf(stderr, "%s:%d:%d: %s: %s\n", d -> path, line, col, kind, msg);
  const char * p = d -> source;
  int current = 1;
  while ( * p && current < line)
    if ( * p++ == '\n') current++;
  const char * end = p;
  while ( * end && * end != '\n') end++;
  fprintf(stderr, "  %.*s\n  ", (int)(end - p), p);
  for (int i = 1; i < col; i++) fputc(p[i - 1] == '\t' ? '\t' : ' ', stderr);
  fputs("^\n", stderr);
  d -> errors++;
}