#include "linked_list.h"
#include "macros.h"

#include <stdlib.h>
#include <stdio.h>

inline static entry *ll_entry_new(void *data)
{
    entry *entry = malloc(sizeof(struct ll_entry));

    if (!entry)
    {
        perror("malloc");
        traceerror();
        return NULL;
    }

    entry->data = data;
    entry->next = NULL;
    entry->prev = NULL;

    return entry;
}

inline static void ll_print(ll *ll)
{
    entry *e;
    int i = 0;
    e = ll->head;
    while (e->next)
    {
        i += 1;
    }
}

inline ll *ll_create()
{
    ll *ll = malloc(sizeof(struct linked_list));
    if (!ll)
    {
        perror("malloc");
        traceerror();
        return NULL;
    }

    ll->head = NULL;
    ll->tail = NULL;
    ll->length = 0;

    return ll;
}

inline ssize_t ll_length(ll *ll)
{
    if (!ll)
    {
        fprintf(stderr, "ll is null");
        traceerror();
        return 0;
    }

    return ll->length;
}

inline int ll_is_empty(ll *ll)
{
    if (!ll)
    {
        fprintf(stderr, "ll is null: %p", (void *)ll);
        traceerror();
        return -1;
    }

    return ll->length == 0;
}

inline int ll_is_full(ll *ll)
{
    if (!ll)
    {
        fprintf(stderr, "ll is null: %p", (void *)ll);
        traceerror();
        return -1;
    }

    return ll_length(ll) == MAX_QUEUE_SIZE;
}

inline void ll_entry_destroy(ll *ll, entry *e)
{
    if (e == NULL)
    {
        return;
    }
    if (e == ll->head)
    {
        return ll_head_pop(ll);
    }
    if (e == ll->tail)
    {
        return ll_tail_pop(ll);
    }

    e->prev->next = e->next;
    e->next->prev = e->prev;

    ll->length--;
    free(e);
}

inline int ll_head_push(ll *ll, void *data)
{

    if (!ll || ll_is_full(ll))
    {
        fprintf(stderr, "pushing to an invalid ll at %p\n", (void *)ll);
        trace();
        return -1;
    }

    entry *entry;
    entry = ll_entry_new(data);
    if (!entry)
    {
        return -1;
    }

    if (!ll->head)
    {
        ll->head = entry;
        ll->tail = entry;
    }

    else
    {
        ll->head->prev = entry;
        ll->head = entry;
    }

    ll->length++;

    return 0;
}

inline void ll_head_pop(ll *ll)
{

    if (ll == NULL || ll_is_empty(ll))
    {
        fprintf(stderr, "popping from an invalid ll");
        traceerror();
        return;
    }

    entry *e;
    e = ll->head;
    ll->head = e->next;

    if (ll->head == NULL)
    {
        ll->tail = NULL;
    }
    else
    {
        ll->head->prev = NULL;
    }

    ll->length--;
    free(e);
}

inline int ll_tail_push(ll *ll, void *data)
{

    if (!ll)
    {
        fprintf(stderr, "pushing to a null ll\n");
        traceerror();
        return -1;
    }

    if (ll_is_full(ll))
    {
        fprintf(stderr, "pushing to a full ll\n");
        traceerror();
        return -1;
    }

    entry *e;

    e = ll_entry_new(data);
    if (!e)
    {
        return -1;
    }

    if (!ll->tail)
    {
        ll->head = e;
        ll->tail = e;
    }

    else
    {
        ll->tail->next = e;
        ll->tail = e;
    }

    ll->length++;
    return 0;
}

inline void ll_tail_pop(ll *ll)
{

    if (!ll || ll_is_empty(ll))
    {
        fprintf(stderr, "popping to an invalid ll");
        traceerror();
        return;
    }

    entry *e;
    e = ll->tail;
    ll->tail = e->prev;

    if (!ll->tail)
    {
        ll->head = NULL;
    }
    else
    {
        ll->tail->next = NULL;
    }

    ll->length--;
    free(e);
}

inline void *ll_head_peek(ll *ll)
{
    if (!ll || ll_is_empty(ll))
    {
        return NULL;
    }
    return ll->head->data;
}

inline void *ll_tail_peek(ll *ll)
{
    if (!ll || ll_is_empty(ll))
    {
        return NULL;
    }
    return ll->tail->data;
}

inline void ll_flush(ll *ll)
{
    while (!ll_is_empty(ll))
    {
        ll_head_pop(ll);
    }

    free(ll);
}