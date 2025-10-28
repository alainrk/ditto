#ifndef DMALLOC_H
#define DMALLOC_H

void *dmalloc(size_t size);
void *drealloc(void *p, size_t size);
void dfree(void *p);
size_t used_memory(void);

#endif
