#include "region.h"

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>

#include "macros.h"

inline static void print_vlocks(vlock *locks[], uint64_t len, handler *handler)
{
    printf("Handler %ld\n", handler->id);
    for (uint64_t i = 0; i < len; i++)
    {
        printf("Lock 0: %8s %ld\n",
               unlocked(atomic_load(locks[i])) ? "UNLOCKED" : "LOCKED",
               getversion(atomic_load(locks[i])));
    }
    fflush(stdout);
}

inline static bool release_vlocks(vlock *vlocks[], uint64_t len)
{
    bool err = true;
    for (uint64_t i = 0; i < len; i++)
    {
        if (!vlock_release(vlocks[i]))
        {
            err = false;
            traceerror();
        }
    }
    return err;
}

inline static bool in_locked_set(vlock *set[], uint64_t len, vlock *lock) // TODO: use cpp hash set
{
    for (uint64_t i = 0; i < len; i++)
    {
        if (lock == set[i])
        {
            return true;
        }
    }
    return false;
}

inline static write_entry *in_write_set(write_entry set[], uint64_t len, const char *addr)
{
    for (uint64_t i = 0; i < len; i++)
    {
        if (addr == set[i].dest)
        {
            return &set[i];
        }
    }
    return NULL;
}

void print_handler(handler *handler)
{
    printf("\t%-15s: %ld\n\t%-15s: %s\n\t%-15s: %ld\n\t%-15s: %ld\n\t%-15s: %ld\n\n",
           "ID", handler->id,
           "Read-only", handler->is_ro ? "TRUE" : "FALSE",
           "Timestamp", handler->timestamp,
           "Read set size", handler->next_ri,
           "Write set size", handler->next_wi);
}

void print_segment(segment *segment)
{
    uint64_t locked = 0;
    for (uint64_t i = 0; i < segment->length; i++)
    {
        if (!unlocked(atomic_load(&segment->vlocks[i])))
        {
            locked += 1;
        }
    }

    printf("\t%-20s: %ld\n\t%-20s: %ld\n\t%-20s: 0x%016lx\n\t%-20s: %ld/%ld\n\n",
           "Index", segment->index,
           "Length", segment->length,
           "Virtual address", (uint64_t)segment->vaddr,
           "Locked words", locked, segment->length);
}

void print_region(region *region)
{
    printf("\t%-20s: %ld\n\t%-20s: %ld\n\t%-20s: %ld\n\t%-20s: %ld\n\n",
           "Alignment", region->alignment,
           "Clock", atomic_load(&region->clock),
           "Created segments", atomic_load(&region->next_segment),
           "Created handlers", atomic_load(&region->next_handler));
}

int segment_init(segment *segment, uint16_t index, size_t size, size_t align)
{
    void *vaddr;

    vaddr = aligned_alloc(align, size);
    if (unlikely(!vaddr))
    {
        perror("malloc");
        traceerror();
        return -1;
    }

    segment->index = index;
    segment->vaddr = vaddr;
    segment->vaddr_base = baseof(vaddr);
    segment->length = size / align;

    for (uint64_t i = 0; i < segment->length; i++)
    {
        atomic_store(&segment->vlocks[i], 0);
        segment->words[i] = &((char *)vaddr)[i * align];
    }

    bzero(segment->vaddr, size);

    return 0;
}

int region_init(region *region, size_t size, size_t align)
{
    region->segments = ll_create();
    if (!region->segments)
    {
        perror("malloc");
        traceerror();
        return -1;
    }

    region->alignment = align;
    atomic_store(&region->clock, 0);
    atomic_store(&region->next_handler, 0);
    atomic_store(&region->next_segment, 1);
    atomic_store(&region->segment_lock, false);

    bzero(region->segments_ctl, MAX_SEGMENTS * sizeof(struct memory_segment));

    if (segment_init(&region->segments_ctl[0], 0, size, align) == -1)
    {
        traceerror();
        return -1;
    }

    if (ll_tail_push(region->segments, region->segments_ctl[0].vaddr) == -1)
    {
        traceerror();
        return -1;
    }
    return 0;
}

void region_destroy(region *region)
{
    ll_flush(region->segments);
}

uintptr_t region_new_tx(region *region, bool is_ro)
{
    handler *handler;
    handler = malloc(sizeof(struct transaction_handler));
    if (!handler)
    {
        perror("malloc");
        traceerror();
        return ~((uintptr_t)0);
    }

    handler->id = atomic_fetch_add(&region->next_handler, 1);
    handler->is_ro = is_ro;
    handler->timestamp = atomic_load(&region->clock);
    handler->next_ri = 0;
    handler->next_wi = 0;
    return (uintptr_t)handler;
}

bool region_write(region *region, handler *handler, void const *src, size_t size, void *dest)
{
    uint64_t n_words;
    void *tmp;
    uint64_t id;

    tmp = malloc(size);
    if (!tmp)
    {
        perror("malloc");
        traceerror();
        return -1;
    }

    /* store src in intermediate pointer */
    memcpy(tmp, src, size);

    n_words = size / region->alignment;
    id = handler->next_wi;
    for (uint64_t i = 0; i < n_words; i++)
    {
        handler_add_write(handler,
                          &((char *)tmp)[i * region->alignment],
                          &((char *)dest)[i * region->alignment],
                          size,
                          id);
    }
    return true;
}

bool ro_read(region *region, handler *unused(handler), void const *src, size_t size, void *dest)
{
    uint64_t n_words;
    uint64_t word_index;
    segment *segment;
    void *src_vaddr;
    void *offset_src, *offset_dest;

    segment = &region->segments_ctl[indexof(src)];
    src_vaddr = vaddrof(src, segment->vaddr_base);

    n_words = size / region->alignment;
    for (uint64_t i = 0; i < n_words; i++)
    {
        offset_src = &(((char *)src_vaddr)[i * region->alignment]);
        offset_dest = &(((char *)dest)[i * region->alignment]);

        word_index = (offset_src - segment->vaddr) / region->alignment;

        memcpy(offset_dest, offset_src, region->alignment);

        /* is the word currently being locked by a different transaction?    */
        /* has the word been updated since this transaction started?         */
        if (!vlock_unlocked_old(&segment->vlocks[word_index], handler->timestamp))
        {
            return false;
        }
    }
    return true;
}

bool rw_read(region *region, handler *handler, void const *src, size_t size, void *dest)
{
    uint64_t n_words;
    uint64_t word_index;
    segment *segment;
    void *src_vaddr;
    write_entry *write;
    void *offset_src, *offset_dest;

    segment = &region->segments_ctl[indexof(src)];
    src_vaddr = vaddrof(src, segment->vaddr_base);

    n_words = size / region->alignment;
    for (uint64_t i = 0; i < n_words; i++)
    {
        offset_src = &((char *)src_vaddr)[i * region->alignment];
        offset_dest = &((char *)dest)[i * region->alignment];

        /* in case of a write before read in the same transaction */
        write = in_write_set(handler->w_set, handler->next_wi, src);
        if (write)
        {
            offset_src = vaddrof(write->src, segment->vaddr_base);
            memcpy(offset_dest, offset_src, region->alignment);
            continue;
        }

        word_index = ((void *)&((char *)src_vaddr)[i * region->alignment] - segment->vaddr) /
                     region->alignment;

        /* is the word currently being locked by a different transaction?    */
        /* has the word been updated since this transaction started?         */
        if (!vlock_unlocked_old(&segment->vlocks[word_index], handler->timestamp))
        {
            return false;
        }

        handler_add_read(handler, &((char *)src)[i * region->alignment]);
        memcpy(offset_dest, offset_src, region->alignment);
    }
    return true;
}

bool region_validate(region *region, handler *handler)
{
    vlock *locked[MAX_SEGMENTS];
    bzero(locked, MAX_SEGMENTS);
    void *dest, *src;
    segment *segment;
    write_entry *write;
    vlock *word_vlock;
    uint64_t vlock_timestamp;
    bool ws_is_multiset;
    uint64_t word_index, lock_index = 0, write_version;

    /* lock write set */
    for (uint64_t i = 0; i < handler->next_wi; i++)
    {
        ws_is_multiset = false;
        dest = handler->w_set[i].dest;
        segment = &region->segments_ctl[indexof(dest)];

        word_index = (vaddrof(dest, segment->vaddr_base) - segment->vaddr) / region->alignment;
        word_vlock = &segment->vlocks[word_index];

        /* check if write already exists in set */
        for (uint64_t j = 0; j < lock_index; j++)
        {

            if (unlikely(locked[j] == word_vlock))
            {
                ws_is_multiset = true;
                break;
            }
        }

        if (ws_is_multiset)
        {
            continue;
        }

        if (!vlock_bounded_spinlock_acquire(word_vlock))
        {
            /* unlock write set and abort transaction */
            if (!release_vlocks(locked, lock_index))
            {
                traceerror();
                print_vlocks(locked, lock_index, handler);
                exit(1);
            }
            // printf("%s(): tx %03ld | abort by spinlock acquisition\n", __FUNCTION__, handler->id);
            return false;
        }
        locked[lock_index++] = word_vlock;
    }

    write_version = atomic_fetch_add(&region->clock, 1) + 1; /* inc-and-fetch */

    /* validate read set */
    if (write_version > handler->timestamp + 1) /* if write_version = handler->timestamp + 1 means no thread    */
                                                /* incremented the global clock since this transaction started  */
    {
        for (uint64_t i = 0; i < handler->next_ri; i++)
        {
            src = handler->r_set[i];
            segment = &region->segments_ctl[indexof(src)];
            word_index = (vaddrof(src, segment->vaddr_base) - segment->vaddr) / region->alignment;
            word_vlock = &segment->vlocks[word_index];

            /* if word is outdated */
            vlock_timestamp = atomic_load(word_vlock);
            if (getversion(vlock_timestamp) > handler->timestamp)
            {
                if (!release_vlocks(locked, lock_index))
                {
                    traceerror();
                    print_vlocks(locked, lock_index, handler);
                    exit(1);
                }
                // printf("%s(): tx %03ld | abort by read set validation (outdated reads)\n", __FUNCTION__, handler->id);
                return false;
            }

            /* if word is locked in validation of a different transaction */
            if (!unlocked(vlock_timestamp) && !in_locked_set(locked, lock_index, word_vlock))
            {
                if (!release_vlocks(locked, lock_index))
                {
                    traceerror();
                    print_vlocks(locked, lock_index, handler);
                    exit(1);
                }
                // printf("%s(): tx %03ld | abort by read set validation (word %ld locked in different transaction)\n", __FUNCTION__, handler->id, word_index);
                return false;
            }
        }
    }

    /* execute writes: do one write per user-called write instead of word-by-word */
    if (handler->next_wi > 0)
    {
        write = &handler->w_set[0];
        memcpy(vaddrof(write->dest, region->segments_ctl[indexof(write->dest)].vaddr_base),
               write->src,
               write->size);

        for (uint64_t i = 1; i < handler->next_wi; i++)
        {
            if (handler->w_set[i].id == write->id)
            {
                continue;
            }
            free(write->src);

            write = &handler->w_set[i];
            memcpy(vaddrof(write->dest, region->segments_ctl[indexof(write->dest)].vaddr_base),
                   write->src,
                   write->size);
        }
    }

    /* update versions */
    for (uint64_t i = 0; i < handler->next_wi; i++)
    {
        dest = handler->w_set[i].dest;
        segment = &region->segments_ctl[indexof(dest)];
        word_index = (vaddrof(dest, segment->vaddr_base) - segment->vaddr) / region->alignment;
        vlock_update(&segment->vlocks[word_index], write_version);
    }

    /* unlock words from write set */
    if (!release_vlocks(locked, lock_index))
    {
        traceerror();
        print_vlocks(locked, lock_index, handler);
        exit(1);
    }
    return true;
}

int region_alloc(region *region, size_t size, void **target)
{
    uint64_t segment_index;
    segment *segment;

    segment_index = atomic_fetch_add(&region->next_segment, 1);
    segment = &region->segments_ctl[segment_index];

    if (unlikely(segment_init(segment, segment_index, size, region->alignment) == -1))
    {
        return 2;
    }

    if (unlikely(!bounded_spinlock_acquire(&region->segment_lock)))
    {
        free((void *)segment->vaddr_base);
        return 1;
    }

    if (unlikely(ll_tail_push(region->segments, segment->vaddr) == -1))
    {
        traceerror();
        if (unlikely(!lock_release(&region->segment_lock)))
        {
            traceerror();
        }
        return 2;
    }

    if (unlikely(!lock_release(&region->segment_lock)))
    {
        traceerror();
    }

    *target = opaqueof(segment->vaddr, segment_index);
    return 0;
}

bool region_free(region *region, handler unused(*handler), void *target)
{
    segment *segment;
    segment = &region->segments_ctl[indexof(target)];

    free(segment->vaddr);

    if (unlikely(!bounded_spinlock_acquire(&region->segment_lock)))
    {
        return false;
    }

    ll_entry_destroy(region->segments, segment->entry);

    if (unlikely(!lock_release(&region->segment_lock)))
    {
        traceerror();
    }
    return true;
}
