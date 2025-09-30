#ifndef fss_h
#define fss_h

#include <stdbool.h>
#include <stddef.h>

#define FSS_DEFAULT_CAP 32

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

FixedSizeStack *fss_create(size_t cap);
void fss_destroy(FixedSizeStack *q);
int fss_push(FixedSizeStack *q, const void *data, size_t size);
void *fss_pop(FixedSizeStack *q, size_t *size);
void *fss_peek(FixedSizeStack *q, size_t n, size_t *size);
size_t fss_size(FixedSizeStack *q);
bool fss_empty(FixedSizeStack *q);

#endif
