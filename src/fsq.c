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
void fsq_push(FixedSizeQueue *q, const void *data, size_t len) {
  // Clean up if busy
  if (q->head->data) {
    free(q->head->data);
  } else {
    // Otherwise we can increment the len counter
    q->len++;
  }

  // Insert new data
  q->head->data = malloc(len);
  if (!q->head->data) {
    perror("malloc newfsq item");
    exit(1);
  }
  memcpy(q->head->data, data, len);

  // Move the head next
  q->head = q->head->next;
}

int main(void) {
  FixedSizeQueue *q = fsq_create(10);
  return 0;
}
