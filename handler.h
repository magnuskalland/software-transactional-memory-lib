#ifndef HANDLER_H
#define HANDLER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "array.h"

#define INIT_WSET_SIZE 3
#define INIT_RSET_SIZE 2048

typedef void *read_entry; /* opaque pointer */

typedef struct write_entry
{
    void *src;  /* virtual address */
    void *dest; /* opaque pointer */
    uint64_t size;
} write_entry;

typedef struct transaction_handler
{
    uint64_t id;
    bool is_ro;
    uint64_t timestamp;
    array *r_set;
    array *w_set;
} handler;

void handler_reset(handler *handler, bool preemptive);
void handler_add_read(handler *handler, read_entry r);
int handler_add_write(handler *handler, void *src, void *dest, uint64_t size);

#endif