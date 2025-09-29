#include "fsq.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FixedSizeQueue *fsq_create(size_t cap) {
  if (cap == 0) {
    cap = FSQ_DEFAULT_CAP;
  }

  FixedSizeQueue *q = malloc(sizeof(FixedSizeQueue));
  if (!q) {
    return NULL;
  }

  q->cap = cap;
  q->len = 0;
  q->head = NULL;

  FSQItem *prev = NULL;
  size_t i = 0;
  do {
    FSQItem *new = malloc(sizeof(FSQItem));
    if (!new) {
      // TODO: destroy should support broken structure
      fsq_destroy(q);
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

void fsq_destroy(FixedSizeQueue *q) {
  if (!q)
    return;

  // Break the circle so to exit at the end in the next loop
  if (q->head)
    q->head->prev->next = NULL;

  FSQItem *curr = q->head;
  while (curr) {
    FSQItem *next = curr->next;
    free(curr->data);
    free(curr);
    curr = next;
  }

  free(q);
}

// Pushes a new element to the queue.
// The head will always point to the next data to be filled/overwritten, so when
// a new element is inserted, if the head is busy already, it means we need to
// clean up the old one and insert the new one. In any case, we move the head
// one place further.
// Returns -1 if there is an error (check errno), 0 otherwise.
int fsq_push(FixedSizeQueue *q, const void *data, size_t size) {
  if (!q)
    return -1;

  // Clean up if busy
  if (q->head->data) {
    free(q->head->data);
  }

  // Insert new data, taking ownership of the internal representation only
  q->head->data = malloc(size);
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
void *fsq_pop(FixedSizeQueue *q, size_t *size) {
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

// Testing function
int main(void) {
  size_t qlen = 10;
  FixedSizeQueue *q = fsq_create(qlen);

  for (int i = 0; i < qlen + 5; i++) {
    char *s = malloc(50);
    sprintf(s, "element %d", i);
    fsq_push(q, s, strlen(s));
    free(s);
  }

  for (int i = 0; i < qlen; i++) {
    size_t len;
    char *s = fsq_pop(q, &len);
    if (!s)
      break;
    printf("popped: %s, q->len = %zu\n", s, q->len);
    free(s);
  }

  return 0;
}
