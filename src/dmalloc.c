#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

static size_t total_mem = 0;

void *dmalloc(size_t size) {
  size_t realsize = size + sizeof(size_t);
  void *p = malloc(realsize);

  if (p == NULL) {
    fprintf(stderr, "Out of memory on dmalloc\n");
    exit(1);
  }

  total_mem += realsize;
  *((size_t*)p) = (size_t)size;

  return p + sizeof(size_t);
}

void *drealloc(void *p, size_t size) {
  if (p == NULL) return NULL;

  void *realp = p - sizeof(size_t);
  size_t oldsize = *((size_t*)(realp));

  void *newp = realloc(realp, size + sizeof(size_t));
  if (p == NULL) {
    fprintf(stderr, "Out of memory on drealloc\n");
    exit(1);
  }

  // Overhead size_t space remains
  total_mem = total_mem - oldsize + size;
  *((size_t*)(newp)) = (size_t)size;

  return newp + sizeof(size_t);
}

void dfree(void *p) {
  if (p == NULL) return;

  void *realp = p - sizeof(size_t);
  size_t objsize = *((size_t*)(realp));
  free(realp);
  total_mem -= (objsize + sizeof(size_t));
}

size_t used_memory(void) {
  return total_mem;
}

#ifdef TESTS_DMALLOC
int main(void) {
  int res;

  // --------- Malloc ---------
  char *s = (char*)dmalloc(100);
  strncpy(s, "test_sss", 8);
  if (used_memory() != 101 + sizeof(size_t)) {
    fprintf(stderr, "Wrong memory usage after alloc 's'\n");
    exit(1);
  }
  res = strncmp(s, "test_sss", 8);
  if (res != 0) {
    fprintf(stderr, "Wrong content of 's' after drealloc = %s\n", s);
    exit(1);
  }

  char *p = (char*)dmalloc(100);
  if (used_memory() != 200 + 2*sizeof(size_t)) {
    fprintf(stderr, "Wrong memory usage after alloc 'p'\n");
    exit(1);
  }

  char *q = (char*)dmalloc(100);
  if (used_memory() != 300 + 3*sizeof(size_t)) {
    fprintf(stderr, "Wrong memory usage after alloc 'q'\n");
    exit(1);
  }

  // --------- Free ---------
  dfree(s);
  if (used_memory() != 200 + 2*sizeof(size_t)) {
    fprintf(stderr, "Wrong memory usage after free 's' = %zu\n", used_memory());
    exit(1);
  }

  dfree(p);
  if (used_memory() != 100 + sizeof(size_t)) {
    fprintf(stderr, "Wrong memory usage after alloc 'p' = %zu\n", used_memory());
    exit(1);
  }

  dfree(q);
  if (used_memory() != 0) {
    fprintf(stderr, "Wrong memory usage after alloc 'q' = %zu\n", used_memory());
    exit(1);
  }

  // --------- Realloc ---------
  char *t = (char*)dmalloc(100);
  strncpy(t, "test", 4);
  t = (char*)drealloc(t, 200);
  if (used_memory() != 200 + sizeof(size_t)) {
    fprintf(stderr, "Wrong memory usage after drealloc 't' = %zu\n", used_memory());
    exit(1);
  }
  res = strncmp(t, "test", 4);
  if (res != 0) {
    fprintf(stderr, "Wrong content of 't' after drealloc = %s\n", t);
    exit(1);
  }
  dfree(t);
  if (used_memory() != 0) {
    fprintf(stderr, "Wrong memory usage after dfree of reallocated 't' = %zu\n", used_memory());
    exit(1);
  }
}
#endif
