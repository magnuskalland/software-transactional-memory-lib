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
 * valgrind --log-file="../359975/valgrind.txt" --dsymutil=yes --track-fds=yes --leak-check=full --show-leak-kinds=all --track-origins=yes ./grading 453 ../reference.so ../359975.so
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

// Internal headers
#include "tm.h"

#include "macros.h"
#include "region.h"
#include "linked_list.h"

// static atomic_ulong commits = {0};
// static atomic_ulong aborts = {0};

/** Create (i.e. allocate + init) a new shared memory region, with one first non-free-able allocated segment of the requested size and alignment.
 * @param size  Size of the first shared segment of memory to allocate (in bytes), must be a positive multiple of the alignment
 * @param align Alignment (in bytes, must be a power of 2) that the shared memory region must support
 * @return Opaque shared memory region handle, 'invalid_shared' on failure
 **/
shared_t tm_create(size_t size, size_t align)
{
    if (unlikely(align & (align - 1) && align)) // is align a the power of 2
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

    region *new_region;
    new_region = malloc(sizeof(region));
    if (unlikely(!new_region))
    {
        perror("malloc");
        traceerror();
        return invalid_shared;
    }

    if (unlikely(region_init(new_region, size, align) == -1))
    {
        traceerror();
        return invalid_shared;
    }

    return new_region;
}

/** Destroy (i.e. clean-up + free) a given shared memory region.
 * @param shared Shared memory region to destroy, with no running transaction
 **/
void tm_destroy(shared_t shared)
{
    region_destroy((region *)shared);
}

/** [thread-safe] Return the start address of the first allocated segment in the shared memory region.
 * @param shared Shared memory region to query
 * @return Start address of the first allocated segment
 **/
void *tm_start(shared_t shared)
{
    return opaqueof(((region *)shared)->segments_ctl[0].vaddr, 0);
}

/** [thread-safe] Return the size (in bytes) of the first allocated segment of the shared memory region.
 * @param shared Shared memory region to query
 * @return First allocated segment size
 **/
size_t tm_size(shared_t shared)
{
    return ((region *)shared)->segments_ctl[0].length *
           ((region *)shared)->alignment;
}

/** [thread-safe] Return the alignment (in bytes) of the memory accesses on the given shared memory region.
 * @param shared Shared memory region to query
 * @return Alignment used globally
 **/
size_t tm_align(shared_t shared)
{
    return ((region *)shared)->alignment;
}

/** [thread-safe] Begin a new transaction on the given shared memory region.
 * @param shared Shared memory region to start a transaction on
 * @param is_ro  Whether the transaction is read-only
 * @return Opaque transaction ID, 'invalid_tx' on failure
 **/
tx_t tm_begin(shared_t shared, bool is_ro)
{
    return region_new_tx((region *)shared, is_ro);
}

/** [thread-safe] End the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to end
 * @return Whether the whole transaction committed
 **/
bool tm_end(shared_t shared, tx_t tx)
{
    if (((handler *)tx)->is_ro)
    {
        handler_reset((handler *)tx, false);
        return true;
    }

    bool commit = region_validate((region *)shared, (handler *)tx);
    handler_reset((handler *)tx, false);
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
    if (((handler *)tx)->is_ro)
    {
        success = ro_read((region *)shared, (handler *)tx, source, size, target);
    }
    else
    {
        success = rw_read((region *)shared, (handler *)tx, source, size, target);
    }

    if (!success)
    {
        handler_reset((handler *)tx, true);
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
    return region_write((region *)shared, (handler *)tx,
                        source, size, target);
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
    return region_alloc((region *)shared, size, target);
}

/** [thread-safe] Memory freeing in the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param target Address of the first byte of the previously allocated segment to deallocate
 * @return Whether the whole transaction can continue
 **/
bool tm_free(shared_t shared, tx_t tx, void *target)
{
    return true;
    return region_free((region *)shared, (handler *)tx, target);
}
