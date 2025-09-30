#ifndef fsq_h
#define fsq_h

#include <stdbool.h>
#include <stddef.h>

#define FSQ_DEFAULT_CAP 32

typedef struct FSSItem {
  void *data;
  size_t size;
  struct FSSItem *next;
  struct FSSItem *prev;

} FSSItem;

typedef struct FixedSizeStack {
  size_t cap;
  size_t len;
  FSSItem *head;
} FixedSizeStack;

FixedSizeStack *fsq_create(size_t cap);
void fsq_destroy(FixedSizeStack *q);
int fsq_push(FixedSizeStack *q, const void *data, size_t size);
void *fsq_pop(FixedSizeStack *q, size_t *size);
void *fsq_peek(FixedSizeStack *q, size_t n, size_t *size);
size_t fsq_size(FixedSizeStack *q);
bool fsq_empty(FixedSizeStack *q);

#endif
