#ifndef REGION_H
#define REGION_H

#include <stddef.h>
#include <stdatomic.h>

#include "sync.h"
#include "linked_list.h"

#define MAX_SEGMENTS 1024 // hard limit 2^16
#define RO_VALIDATE_ATTEMPTS 10

#define nbytemask(n) ((uint64_t)((((uint64_t)1) << (8 * n)) - 1))

#define opaqueof(vaddr, i) \
    ((void *)(((uint64_t)vaddr & (nbytemask(6))) | ((uint64_t)i) << 48))

#define vaddrof(opaque, base) \
    ((void *)((nbytemask(6) & (uint64_t)opaque) + (uint64_t)base))

#define baseof(vaddr) \
    (((uint64_t)vaddr) & (nbytemask(2) << 48))

#define indexof(opaque) \
    (((uint64_t)opaque) >> 48)

#define region(s) ((region *)(s))
#define segments(shared) ((array *)region(shared)->segments)
#define getsegment(segments, index) \
    ((segment *)arrayget(segments, index))

typedef struct memory_segment
{
    uint64_t index;
    uint64_t length;
    uint64_t vaddr_base;
    void *vaddr;   /* heap */
    vlock *vlocks; /* heap */
} segment;

typedef struct memory_region
{
    size_t alignment;
    atomic_ulong clock;
    atomic_ulong segment_count;
    atomic_ulong next_segment;
    atomic_ulong next_handler;        // TODO: remove
    struct memory_segment **segments; /* heap */
    lock segment_lock;
    ll *alloced_list; /* heap */
    ll *freed_list;   /* heap */
} region;

#endif