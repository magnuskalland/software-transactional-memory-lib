#ifndef QUEUE_H
#define QUEUE_H

#include <sys/types.h>

#define MAX_QUEUE_SIZE 512

typedef struct ll_entry
{
    struct ll_entry *next;
    struct ll_entry *prev;
    void *data;
} entry;

typedef struct linked_list
{
    struct ll_entry *head;
    struct ll_entry *tail;
    size_t length;
} ll;

ll *ll_create();
ssize_t ll_length(ll *ll);

int ll_is_empty(ll *ll);
int ll_is_full(ll *ll);

int ll_head_push(ll *ll, void *data);
int ll_tail_push(ll *ll, void *data);

void ll_head_pop(ll *ll);
void ll_tail_pop(ll *ll);

void *ll_head_peek(ll *ll);
void *ll_tail_peek(ll *ll);

void ll_entry_destroy(ll *ll, entry *entry);
void ll_flush(ll *ll);

#endif