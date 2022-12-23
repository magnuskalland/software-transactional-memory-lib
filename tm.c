/**
 * @file   tm.c
 * @author [...]
 *
 * @section LICENSE
 *
 * [...]
 *
 * @section DESCRIPTION
 *
 * Implementation of your own transaction manager.
 * You can completely rewrite this file (and create more files) as you wish.
 * Only the interface (i.e. exported symbols and semantic) must be preserved.
 **/

/**
 * valgrind --log-file="../359975/valgrind.txt" --dsymutil=yes --track-fds=yes --leak-check=full --show-leak-kinds=all --track-origins=yes ./grading 537 ../reference.so ../359975.so
 * valgrind --log-file="../359975/valgrind.txt" --tool=exp-sgcheck ./grading 537 ../359975.so ../reference.so
 */

// Requested features
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#ifdef __STDC_NO_ATOMICS__
#error Current C11 compiler does not support atomic operations
#endif

// External headers
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

// Internal headers
#include "array.h"
#include "handler.h"
#include "linked_list.h"
#include "macros.h"
#include "region.h"
#include "sync.h"
#include "tm.h"
#include "utils.h"

static bool ro_read(region *region, handler *handler, void const *src, size_t size, void *dest);
static bool rw_read(region *region, handler *handler, void const *src, size_t size, void *dest);
static bool ro_validate(region *region, handler *handler);
static segment *segment_create(uint16_t index, size_t size, size_t align);
static bool transaction_validate(region *region, handler *handler);
static void flush_segment_ll(ll *ll);

/** Create (i.e. allocate + init) a new shared memory region, with one first non-free-able allocated segment of the requested size and alignment.
 * @param size  Size of the first shared segment of memory to allocate (in bytes), must be a positive multiple of the alignment
 * @param align Alignment (in bytes, must be a power of 2) that the shared memory region must support
 * @return Opaque shared memory region handle, 'invalid_shared' on failure
 **/
shared_t tm_create(size_t size, size_t align)
{
    if (unlikely(align & (align - 1) && align)) // is align in the power of 2
    {
        fprintf(stderr, "align %ld not a power of 2\n", align);
        return invalid_shared;
    }
    if (unlikely(size % align != 0)) // is size a multiplier of align
    {
        fprintf(stderr, "size %ld is not a multiplier of align %ld\n", size, align);
        return invalid_shared;
    }
    if (unlikely(size > MSS)) // is size bigger than max segment size
    {
        fprintf(stderr, "size %ld bigger than max segment size %ld\n", size, MSS);
        return invalid_shared;
    }

    struct memory_region *region;

    region = malloc(sizeof(struct memory_region));
    if (unlikely(!region))
    {
        perror("malloc");
        traceerror();
        return invalid_shared;
    }

    region->alignment = align;
    region->clock = 0;
    region->next_handler = 0;
    region->segment_count = 1;
    region->next_segment = 1;
    region->segment_lock = false;

    region->segments = malloc(sizeof(struct memory_segment *) * MAX_SEGMENTS);
    if (!region->segments)
    {
        perror("malloc");
        traceerror();
        return invalid_shared;
    }

    region->alloced_list = ll_create();
    if (!region->alloced_list)
    {
        traceerror();
        return invalid_shared;
    }

    region->freed_list = ll_create();
    if (!region->freed_list)
    {
        traceerror();
        return invalid_shared;
    }
    region->segments[0] = segment_create(0, size, align);
    if (!region->freed_list)
    {
        traceerror();
        return invalid_shared;
    }
    ll_tail_push(region->alloced_list, region->segments[0]);
    return region;
}

/** Destroy (i.e. clean-up + free) a given shared memory region.
 * @param shared Shared memory region to destroy, with no running transaction
 **/
void tm_destroy(shared_t shared)
{
    struct memory_region *region = (struct memory_region *)shared;
    flush_segment_ll(region->freed_list);
    flush_segment_ll(region->alloced_list);
    free(region->alloced_list);
    free(region->freed_list);
    free(region->segments);
    free(region);
}

/** [thread-safe] Return the start address of the first allocated segment in the shared memory region.
 * @param shared Shared memory region to query
 * @return Start address of the first allocated segment
 **/
void *tm_start(shared_t shared)
{
    return opaqueof(((struct memory_region *)shared)->segments[0]->vaddr, 0);
}

/** [thread-safe] Return the size (in bytes) of the first allocated segment of the shared memory region.
 * @param shared Shared memory region to query
 * @return First allocated segment size
 **/
size_t tm_size(shared_t shared)
{
    return ((struct memory_region *)shared)->segments[0]->length * ((struct memory_region *)shared)->alignment;
}

/** [thread-safe] Return the alignment (in bytes) of the memory accesses on the given shared memory region.
 * @param shared Shared memory region to query
 * @return Alignment used globally
 **/
size_t tm_align(shared_t shared)
{
    return ((struct memory_region *)shared)->alignment;
}

/** [thread-safe] Begin a new transaction on the given shared memory region.
 * @param shared Shared memory region to start a transaction on
 * @param is_ro  Whether the transaction is read-only
 * @return Opaque transaction ID, 'invalid_tx' on failure
 **/
tx_t tm_begin(shared_t shared, bool is_ro)
{
    struct transaction_handler *handler;

    handler = malloc(sizeof(struct transaction_handler));
    if (unlikely(!handler))
    {
        perror("malloc");
        traceerror();
        return invalid_tx;
    }

    handler->id = atomic_fetch_add(&((region *)shared)->next_handler, 1);
    handler->is_ro = is_ro;
    handler->timestamp = atomic_load(&((struct memory_region *)shared)->clock);
    handler->r_set = array_init_size(INIT_RSET_SIZE);
    handler->w_set = array_init_size(INIT_WSET_SIZE);

    return (tx_t)handler;
}

/** [thread-safe] End the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to end
 * @return Whether the whole transaction committed
 **/
bool tm_end(shared_t shared, tx_t tx)
{
    if (((struct transaction_handler *)tx)->is_ro)
    {
        handler_reset((struct transaction_handler *)tx, false);
        return true;
    }

    bool commit = transaction_validate((struct memory_region *)shared, (struct transaction_handler *)tx);
    handler_reset((struct transaction_handler *)tx, false);
    return commit;
}

/** [thread-safe] Read operation in the given transaction, source in the shared region and target in a private region.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param source Source start address (in the shared region)
 * @param size   Length to copy (in bytes), must be a positive multiple of the alignment
 * @param target Target start address (in a private region)
 * @return Whether the whole transaction can continue
 **/
bool tm_read(shared_t shared, tx_t tx, void const *source, size_t size, void *target)
{
    bool success;
    if (((struct transaction_handler *)tx)->is_ro)
    {
        success = ro_read((struct memory_region *)shared, (struct transaction_handler *)tx,
                          source, size, target);
    }
    else
    {
        success = rw_read((struct memory_region *)shared, (struct transaction_handler *)tx,
                          source, size, target);
    }
    if (!success)
    {
        handler_reset((struct transaction_handler *)tx, true);
    }
    return success;
}

/** [thread-safe] Write operation in the given transaction, source in a private region and target in the shared region.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param source Source start address (in a private region)
 * @param size   Length to copy (in bytes), must be a positive multiple of the alignment
 * @param target Target start address (in the shared region)
 * @return Whether the whole transaction can continue
 **/
bool tm_write(shared_t shared, tx_t tx, void const *source, size_t size, void *target)
{
    struct memory_region *region;
    struct transaction_handler *handler;
    uint64_t n_words;
    void *tmp, *offset_src, *offset_dest;

    region = (struct memory_region *)shared;
    handler = (struct transaction_handler *)tx;

    n_words = size / region->alignment;
    for (uint64_t i = 0; i < n_words; i++)
    {
        offset_src = &((char *)source)[i * region->alignment];
        offset_dest = &((char *)target)[i * region->alignment];
        tmp = malloc(region->alignment);
        memcpy(tmp, offset_src, region->alignment);
        handler_add_write(handler, tmp, offset_dest, size);
    }
    return true;
}

/** [thread-safe] Memory allocation in the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param size   Allocation requested size (in bytes), must be a positive multiple of the alignment
 * @param target Pointer in private memory receiving the address of the first byte of the newly allocated, aligned segment
 * @return Whether the whole transaction can continue (success/nomem), or not (abort_alloc)
 **/
alloc_t tm_alloc(shared_t shared, tx_t unused(tx), size_t size, void **target)
{
    struct memory_region *region;
    segment *segment;
    uint64_t segment_index;
    region = (struct memory_region *)shared;

    segment_index = atomic_fetch_add(&region->next_segment, 1);
    if (unlikely(segment_index >= MAX_SEGMENTS))
    {
        fprintf(stderr, "warning: max segments %d exceeded\n", MAX_SEGMENTS);
        return nomem_alloc;
    }

    segment = segment_create(segment_index, size, region->alignment);
    if (unlikely(!segment))
    {
        return nomem_alloc;
    }

    if (unlikely(!bounded_spinlock_acquire(&region->segment_lock)))
    {
        free(segment->vaddr);
        free(segment);
        return abort_alloc;
    }

    /* free marked segments before allocating new */
    flush_segment_ll(region->freed_list);

    ll_tail_push(region->alloced_list, segment);
    region->segments[segment_index] = segment;

    if (unlikely(!lock_release(&region->segment_lock)))
    {
        traceerror();
    }

    atomic_fetch_add(&region->segment_count, 1);
    *target = opaqueof(segment->vaddr, segment_index);
    return success_alloc;
}

/** [thread-safe] Memory freeing in the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param target Address of the first byte of the previously allocated segment to deallocate
 * @return Whether the whole transaction can continue
 **/
bool tm_free(shared_t shared, tx_t unused(tx), void *target)
{
    struct memory_region *region;
    segment *segment;

    region = (struct memory_region *)shared;
    segment = region->segments[indexof(target)];

    if (unlikely(!bounded_spinlock_acquire(&region->segment_lock)))
    {
        return false;
    }

    /* make sure it's safe that multiple transactions free the same segment */
    if (likely(ll_remove(region->alloced_list, segment)))
    {
        ll_tail_push(region->freed_list, segment);
    }

    if (unlikely(!lock_release(&region->segment_lock)))
    {
        traceerror();
    }

    return true;
}

bool ro_read(region *region, handler *handler, void const *src, size_t size, void *dest)
{
    segment *segment;
    void *src_vaddr, *offset_src, *offset_dest;
    uint64_t n_words, word_index, timestamp, attempts = 0;

    segment = region->segments[indexof(src)];
    src_vaddr = vaddrof(src, segment->vaddr_base);

    n_words = size / region->alignment;
    for (uint64_t i = 0; i < n_words; i++)
    {
        offset_src = &(((char *)src_vaddr)[i * region->alignment]);
        offset_dest = &(((char *)dest)[i * region->alignment]);
        memcpy(offset_dest, offset_src, region->alignment);

        word_index = (offset_src - segment->vaddr) / region->alignment;

        /* without ro optimization */
        // if (!vlock_unlocked_old(&segment->vlocks[word_index], handler->timestamp))
        // {
        //     return false;
        // }

        /* is the word currently being locked by a different transaction?    */
        /* has the word been updated since this transaction started?         */
        while (!vlock_unlocked_old(&segment->vlocks[word_index], handler->timestamp))
        {
            timestamp = atomic_load(&region->clock);
            if (!ro_validate(region, handler))
            {
                // printf("%s(): tx %08ld | abort by read set validation\n", __FUNCTION__, handler->id);
                return false;
            }
            handler->timestamp = timestamp;
            memcpy(offset_dest, offset_src, region->alignment);
            if (++attempts == RO_VALIDATE_ATTEMPTS)
            {
                // printf("%s(): tx %08ld | abort by exceeded attempts\n", __FUNCTION__, handler->id);
                return false;
            }
        }
        handler_add_read(handler, &((char *)src)[i * region->alignment]);
    }
    return true;
}

bool rw_read(region *region, handler *handler, void const *src, size_t size, void *dest)
{
    segment *segment;
    write_entry *write;
    uint64_t n_words, word_index;
    void *src_vaddr, *offset_src, *offset_dest;

    segment = region->segments[indexof(src)];
    src_vaddr = vaddrof(src, segment->vaddr_base);

    n_words = size / region->alignment;
    for (uint64_t i = 0; i < n_words; i++)
    {
        offset_src = &((char *)src_vaddr)[i * region->alignment];
        offset_dest = &((char *)dest)[i * region->alignment];

        word_index = ((void *)&((char *)src_vaddr)[i * region->alignment] - segment->vaddr) /
                     region->alignment;

        /* is the word currently being locked by a different transaction?    */
        /* has the word been updated since this transaction started?         */
        if (!vlock_unlocked_old(&segment->vlocks[word_index], handler->timestamp))
        {
            return false;
        }
        /* in case of a write before read in the same transaction */
        write = in_write_set(handler->w_set, src);
        if (write)
        {
            offset_src = vaddrof(write->src, segment->vaddr_base);
            memcpy(offset_dest, offset_src, region->alignment);
            continue;
        }
        handler_add_read(handler, &((char *)src)[i * region->alignment]);
        memcpy(offset_dest, offset_src, region->alignment);
    }
    return true;
}

static bool ro_validate(region *region, handler *handler)
{
    segment *segment;
    void *src;
    uint64_t vlock_timestamp, word_index;
    for (uint64_t i = 0; i < handler->r_set->size; i++)
    {
        src = arrayget(handler->r_set, i);
        segment = region->segments[indexof(src)];
        word_index = (vaddrof(src, segment->vaddr_base) - segment->vaddr) / region->alignment;

        /* if word is outdated */
        vlock_timestamp = atomic_load(&segment->vlocks[word_index]);
        /* locked bit is MSB and we therefore check for both version and if-locked */
        /* if (word is newer than recorded timestamp) OR (word is locked) */
        if (vlock_timestamp > handler->timestamp)
        {
            return false;
        }
    }
    return true;
}

segment *segment_create(uint16_t index, size_t size, size_t align)
{
    segment *segment;
    segment = malloc(sizeof(struct memory_segment));
    if (unlikely(!segment))
    {
        perror("malloc");
        traceerror();
        return NULL;
    }

    segment->vaddr = aligned_alloc(align, size);
    if (unlikely(!segment->vaddr))
    {
        perror("malloc");
        traceerror();
        return NULL;
    }
    bzero(segment->vaddr, size);

    segment->index = index;
    segment->length = size / align;
    segment->vaddr_base = baseof(segment->vaddr);

    segment->vlocks = calloc(sizeof(vlock), segment->length);
    if (unlikely(!segment->vlocks))
    {
        perror("malloc");
        traceerror();
        return NULL;
    }

    return segment;
}

void flush_segment_ll(ll *ll)
{
    entry *e;
    while (ll_length(ll) > 0)
    {
        e = ll->head;
        free(((struct memory_segment *)e->data)->vaddr);
        free(((struct memory_segment *)e->data)->vlocks);
        ll_entry_destroy(ll, e);
    }
}

bool transaction_validate(region *region, handler *handler)
{
    array *locked;
    segment *segment;
    write_entry *write;
    vlock *word_vlock;
    void *dest, *src;
    uint64_t vlock_timestamp, word_index, write_version;

    locked = array_init_size(INIT_WSET_SIZE);

    /* lock write set */
    for (uint64_t i = 0; i < handler->w_set->size; i++)
    {
        dest = ((write_entry *)arrayget(handler->w_set, i))->dest;
        segment = region->segments[indexof(dest)];

        word_index = (vaddrof(dest, segment->vaddr_base) - segment->vaddr) / region->alignment;
        word_vlock = &segment->vlocks[word_index];

        if (in_set(locked, word_vlock))
        {
            continue;
        }

        if (!vlock_bounded_spinlock_acquire(word_vlock))
        {
            /* unlock write set and abort transaction */
            // printf("%s(): tx %08ld | abort by spinlock acquisition\n", __FUNCTION__, handler->id);
            release_vlocks(locked);
            array_destroy(locked);
            return false;
        }
        array_add(&locked, word_vlock);
    }

    write_version = atomic_fetch_add(&region->clock, 1) + 1; /* inc-and-fetch */

    /* validate read set */
    if (write_version > handler->timestamp + 1) /* if write_version = handler->timestamp + 1 means no thread    */
                                                /* incremented the global clock since this transaction started  */
    {
        for (uint64_t i = 0; i < handler->r_set->size; i++)
        {
            src = arrayget(handler->r_set, i);
            segment = region->segments[indexof(src)];
            word_index = (vaddrof(src, segment->vaddr_base) - segment->vaddr) / region->alignment;
            word_vlock = &segment->vlocks[word_index];

            /* if word is outdated */
            vlock_timestamp = atomic_load(word_vlock);
            if (getversion(vlock_timestamp) > handler->timestamp)
            {
                // printf("%s(): tx %08ld | abort by read set validation (outdated reads)\n", __FUNCTION__, handler->id);
                release_vlocks(locked);
                array_destroy(locked);
                return false;
            }

            /* if word is locked in validation of a different transaction */
            if (locked(vlock_timestamp) && !in_set(locked, word_vlock))
            {
                // printf("%s(): tx %08ld | abort by read set validation (word %ld locked in different transaction)\n", __FUNCTION__, handler->id, word_index);
                release_vlocks(locked);
                array_destroy(locked);
                return false;
            }
        }
    }

    /* store write set word-by-word */
    for (uint64_t i = 0; i < handler->w_set->size; i++)
    {
        write = arrayget(handler->w_set, i);
        segment = region->segments[indexof(write->dest)];
        word_index = (vaddrof(write->dest, segment->vaddr_base) - segment->vaddr) / region->alignment;
        memcpy(vaddrof(write->dest, segment->vaddr_base), write->src, write->size);
        free(write->src);
        vlock_update(&segment->vlocks[word_index], write_version);
    }

    release_vlocks(locked);
    array_destroy(locked);
    return true;
}
