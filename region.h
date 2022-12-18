#include <stddef.h>
#include <stdatomic.h>

#include "handler.h"
#include "linked_list.h"
#include "sync.h"

#define MAX_SEGMENTS 512 // hard limit 2^16
#define MAX_FREED_SEGMENTS 8
#define MAX_WORDS 256

#define nbytemask(n) ((uint64_t)((((uint64_t)1) << (8 * n)) - 1))

#define opaqueof(vaddr, i) \
    ((void *)(((uint64_t)vaddr & (nbytemask(6))) | ((uint64_t)i) << 48))

#define vaddrof(opaque, base) \
    ((void *)((nbytemask(6) & (uint64_t)opaque) + (uint64_t)base))

#define baseof(vaddr) \
    (((uint64_t)vaddr) & (nbytemask(2) << 48))

#define indexof(opaque) \
    (((uint64_t)opaque) >> 48)

typedef struct memory_segment
{
    uint64_t index;
    uint64_t length;
    void *vaddr;
    uint64_t vaddr_base;
    vlock vlocks[MAX_WORDS];
    void *words[MAX_WORDS];
    entry *entry;
} segment;

typedef struct memory_region
{
    size_t alignment;
    atomic_ulong clock;

    ll *segments;
    segment segments_ctl[MAX_SEGMENTS];
    atomic_ulong next_segment;

    atomic_ulong next_handler;
    lock segment_lock;

    atomic_ulong next_free;
    atomic_ulong free_counter;
    segment *free_segments_cache[MAX_FREED_SEGMENTS];
} region;

void print_region(region *region);
void print_handler(handler *handler);
void print_segment(segment *segment);

int region_init(region *region, size_t size, size_t align);
int segment_init(segment *segment, uint16_t index, size_t size, size_t align);
void region_destroy(region *region);
uintptr_t region_new_tx(region *region, bool is_ro);
bool region_validate(region *region, handler *handler);

bool region_write(region *region, handler *handler, void const *src, size_t size, void *dest);
bool ro_read(region *region, handler *handler, void const *src, size_t size, void *dest);
bool rw_read(region *region, handler *handler, void const *src, size_t size, void *dest);

int region_alloc(region *region, size_t size, void **target);
bool region_free(region *region, handler *handler, void *target);