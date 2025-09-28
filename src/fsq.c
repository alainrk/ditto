#include "fsq.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

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
    new->element = NULL;
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
    free(curr->element);
    free(curr);
    curr = next;
  }

  free(q);
}

void fsq_push(FixedSizeQueue *q, void *element) {}

int main(void) {
  FixedSizeQueue *q = fsq_create(10);
  return 0;
}
