#ifndef _H_LIST

#include <stdint.h>

struct list;

struct list *
list_new (void);

void
list_add (struct list *l, void *item);

void *
list_get (struct list *l, uint64_t i);

uint64_t
list_count (struct list *l);

void
list_free (struct list *l);

#endif
