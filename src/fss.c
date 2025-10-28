#include "fss.h"
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dmalloc.h"

FixedSizeStack *fss_create(size_t cap) {
  if (cap == 0) {
    cap = FSS_DEFAULT_CAP;
  }

  FixedSizeStack *q = dmalloc(sizeof(FixedSizeStack));
  if (!q) {
    return NULL;
  }

  q->cap = cap;
  q->len = 0;
  q->head = NULL;

  FSSItem *prev = NULL;
  size_t i = 0;
  do {
    FSSItem *new = dmalloc(sizeof(FSSItem));
    if (!new) {
      // TODO: destroy should support broken structure
      fss_destroy(q);
      return NULL;
    }
    new->data = NULL;
    new->size = 0;
    new->next = NULL;
    new->prev = prev;

    if (prev) {
      prev->next = new;
    } else {
      q->head = new;
    }

    prev = new;
    i++;
  } while (i < cap);

  q->head->prev = prev;
  prev->next = q->head;

  return q;
}

void fss_destroy(FixedSizeStack *q) {
  if (!q)
    return;

  // Break the circle so to exit at the end in the next loop
  if (q->head)
    q->head->prev->next = NULL;

  FSSItem *curr = q->head;
  while (curr) {
    FSSItem *next = curr->next;
    dfree(curr->data);
    dfree(curr);
    curr = next;
  }

  dfree(q);
}

// Pushes a new element to the queue.
// The head will always point to the next data to be filled/overwritten, so when
// a new element is inserted, if the head is busy already, it means we need to
// clean up the old one and insert the new one. In any case, we move the head
// one place further.
// Returns -1 if there is an error (check errno), 0 otherwise.
int fss_push(FixedSizeStack *q, const void *data, size_t size) {
  if (!q)
    return -1;

  // Clean up if busy
  if (q->head->data) {
    dfree(q->head->data);
  }

  // Insert new data, taking ownership of the internal representation only
  q->head->data = dmalloc(size);
  if (!q->head->data) {
    errno = ENOMEM;
    return -1;
  }

  memcpy(q->head->data, data, size);
  q->head->size = size;

  // Move the head next
  q->head = q->head->next;

  // Increase the length counter
  q->len = q->cap > q->len ? q->len + 1 : q->len;

  return 0;
}

// Pops an element from the queue. If the queue is empty, returns NULL.
void *fss_pop(FixedSizeStack *q, size_t *size) {
  if (!q)
    return NULL;

  if (q->len == 0)
    return NULL;

  // Get the data, where the first one is the newest, and set on the previous of
  // the head
  if (!q->head || !q->head->prev || !q->head->prev->data) {
    return NULL;
  }

  void *data = q->head->prev->data;
  if (size)
    *size = q->head->prev->size;

  q->head->prev->data = NULL;
  q->head->prev->size = 0;

  // Move the head one place backward and decrease the len counter
  q->head = q->head->prev;
  q->len--;

  return data;
}

bool fss_empty(FixedSizeStack *q) { return q->len == 0; }

size_t fss_size(FixedSizeStack *q) { return q->len; }

// Peeks the nth element from the queue, NULL otherwise.
// n = 0 means peeking the first element.
void *fss_peek(FixedSizeStack *q, size_t n, size_t *size) {
  if (!q)
    return NULL;

  if (q->len == 0)
    return NULL;

  if (n > q->cap - 1 || n > q->len - 1)
    return NULL;

  if (!q->head || !q->head->prev) {
    return NULL;
  }

  // Loop backward to get the n-th element from the head (right after last
  // inserted element)
  FSSItem *curr = q->head->prev;
  while (n > 0) {
    curr = curr->prev;
    n--;
  }

  if (curr->data == NULL)
    return NULL;

  // Insert new data, taking ownership of the internal representation only
  void *res = dmalloc(curr->size);
  if (!res) {
    errno = ENOMEM;
    return NULL;
  }

  memcpy(res, curr->data, curr->size);
  *size = curr->size;

  return res;
}


#ifdef TESTS_FSS
int main(void) {
  size_t qlen = 10;
  FixedSizeStack *q = fss_create(qlen);

  for (int i = 0; i < qlen + 5; i++) {
    char *s = dmalloc(50);
    sprintf(s, "element %d", i);
    fss_push(q, s, strlen(s));
    dfree(s);
  }

  for (int i = 0; i < qlen + 2; i++) {
    size_t len;
    char *s = fss_peek(q, i, &len);
    if (!s) {
      printf("NULL - break\n");
      break;
    }
    printf("peeked: %s, len: %zu, q->len = %zu\n", s, len, q->len);
    dfree(s);
  }

  for (int i = 0; i < qlen; i++) {
    size_t len;
    char *s = fss_pop(q, &len);
    if (!s)
      break;
    printf("popped: %s, q->len = %zu\n", s, q->len);
    dfree(s);
  }

  return 0;
}
#endif
