#include "handler.h"

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <stdatomic.h>

#include "macros.h"

void handler_reset(handler *handler, bool preemptive)
{
    for (uint64_t i = 0; i < handler->w_set->size; i++)
    {
        if (preemptive)
        {
            free(((write_entry *)arrayget(handler->w_set, i))->src);
        }
        free(arrayget(handler->w_set, i));
    }
    array_destroy(handler->w_set);
    array_destroy(handler->r_set);
    free(handler);
}

inline void handler_add_read(handler *handler, read_entry r)
{
    array_add(&handler->r_set, r);
}

inline int handler_add_write(handler *handler, void *src, void *dest, uint64_t size)
{
    write_entry *e = malloc(sizeof(write_entry));
    if (!e)
    {
        perror("malloc");
        return -1;
    }

    e->size = size;
    e->src = src;
    e->dest = dest;

    array_add(&handler->w_set, e);
    return 0;
}