#include "handler.h"

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <stdatomic.h>

#include "macros.h"

void handler_reset(handler *handler, bool preemptive)
{
    if (preemptive)
    {
        for (uint64_t i = 0; i < handler->next_wi; i++)
        {
            free(handler->w_set[i].src);
        }
    }
    free(handler);
}

inline int handler_add_read(handler *handler, read_entry r)
{
    if (unlikely(handler->next_ri == READ_SET_MAX - 1))
    {
        fprintf(stderr, "%s(): max read set size exhausted, increase limit\n", __FUNCTION__);
        traceerror();
        return -1;
    }

    handler->r_set[handler->next_ri] = r;

    handler->next_ri += 1;
    return 0;
}

inline int handler_add_write(handler *handler, void *src, void *dest, uint64_t size, uint64_t id)
{
    if (unlikely(handler->next_wi == WRITE_SET_MAX - 1))
    {
        fprintf(stderr, "%s(): max write set size exhausted, increase limit\n", __FUNCTION__);
        traceerror();
        return -1;
    }

    handler->w_set[handler->next_wi].src = src;
    handler->w_set[handler->next_wi].dest = dest;
    handler->w_set[handler->next_wi].size = size;
    handler->w_set[handler->next_wi].id = id;

    handler->next_wi += 1;
    return 0;
}