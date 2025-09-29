#ifndef fsq_h
#define fsq_h

#include <stdbool.h>
#include <stddef.h>

#define FSQ_DEFAULT_CAP 32

typedef struct FSQItem {
  void *data;
  struct FSQItem *next;
  struct FSQItem *prev;

} FSQItem;

typedef struct FixedSizeQueue {
  size_t cap;
  size_t len;
  FSQItem *head;
} FixedSizeQueue;

FixedSizeQueue *fsq_create(size_t cap);
void fsq_destroy(FixedSizeQueue *q);
void fsq_push(FixedSizeQueue *q, const void *data, size_t len);
void *fsq_pop(FixedSizeQueue *q);
void *fsq_peek(FixedSizeQueue *q, size_t n);
size_t fsq_size(FixedSizeQueue *q);
bool fsq_empty(FixedSizeQueue *q);

#endif
