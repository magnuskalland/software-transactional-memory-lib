#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "linked_list.h"

#define READ_SET_MAX 2048
#define WRITE_SET_MAX 2048

/* opaque */
typedef void *read_entry;

typedef struct write_entry
{
    uint64_t id;
    /* virtual address */
    void *src;
    /* opaque pointer */
    void *dest;
    uint64_t size;
} write_entry;

typedef struct transaction_handler
{
    uint64_t id; // TODO: remove

    bool is_ro;
    uint64_t timestamp;

    read_entry r_set[READ_SET_MAX];
    uint64_t next_ri;

    write_entry w_set[WRITE_SET_MAX];
    uint64_t next_wi;
} handler;

void handler_reset(handler *handler, bool preemptive);
int handler_add_read(handler *handler, read_entry r);
int handler_add_write(handler *handler, void *src, void *dest, uint64_t size, uint64_t id);
