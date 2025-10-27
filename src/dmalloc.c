#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

static size_t used_memory = 0;

void *dmalloc(size_t size) {
  size_t realsize = size + sizeof(size_t);
  void *p = malloc(realsize);

  if (p == NULL) {
    fprintf(stderr, "Out of memory on dmalloc\n");
    exit(1);
  }

  used_memory += realsize;
  *((size_t*)p) = (size_t)size;

  return p + sizeof(size_t);
}

void dfree(void *p) {
  void *realp = p - sizeof(size_t);
  if (p == NULL) return;
  size_t objsize = *((size_t*)(realp));
  free(realp);
  used_memory -= (objsize + sizeof(size_t));
}

size_t dused_memory() {
  return used_memory;
}

// Test implementation
int main() {
  char *s = (char*)dmalloc(100);
  if (dused_memory() != 100 + sizeof(size_t)) {
    fprintf(stderr, "Wrong memory usage after alloc 's'\n");
    exit(1);
  }

  char *p = (char*)dmalloc(100);
  if (dused_memory() != 200 + 2*sizeof(size_t)) {
    fprintf(stderr, "Wrong memory usage after alloc 'p'\n");
    exit(1);
  }

  char *q = (char*)dmalloc(100);
  if (dused_memory() != 300 + 3*sizeof(size_t)) {
    fprintf(stderr, "Wrong memory usage after alloc 'q'\n");
    exit(1);
  }

  dfree(s);
  if (dused_memory() != 200 + 2*sizeof(size_t)) {
    fprintf(stderr, "Wrong memory usage after free 's' = %zu\n", dused_memory());
    exit(1);
  }

  dfree(p);
  if (dused_memory() != 100 + sizeof(size_t)) {
    fprintf(stderr, "Wrong memory usage after alloc 'p' = %zu\n", dused_memory());
    exit(1);
  }

  dfree(q);
  if (dused_memory() != 0) {
    fprintf(stderr, "Wrong memory usage after alloc 'q' = %zu\n", dused_memory());
    exit(1);
  }
}
