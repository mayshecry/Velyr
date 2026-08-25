#include "velyr.h"

void die_oom(void) {
  fputs("velyr: out of memory\n", stderr);
  exit(2);
}

void * arena_alloc(Arena * a, size_t n) {
  n = (n + 7u) & ~7u;
  if (!a -> head || a -> head -> used + n > a -> head -> cap) {
    size_t cap = n > 65536 ? n : 65536;
    ArenaChunk * c = malloc(sizeof( * c) + cap);
    if (!c) die_oom();
    c -> next = a -> head;
    c -> used = 0;
    c -> cap = cap;
    a -> head = c;
  }
  void * p = a -> head -> data + a -> head -> used;
  a -> head -> used += n;
  memset(p, 0, n);
  return p;
}

char * arena_strndup(Arena * a,
  const char * s, size_t n) {
  char * r = arena_alloc(a, n + 1);
  memcpy(r, s, n);
  r[n] = 0;
  return r;
}

void arena_free(Arena * a) {
  while (a -> head) {
    ArenaChunk * n = a -> head -> next;
    free(a -> head);
    a -> head = n;
  }
}
