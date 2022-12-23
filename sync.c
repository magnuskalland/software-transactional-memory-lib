#include "sync.h"

#include <stdint.h>
#include <stdio.h>

#include "macros.h"

/**
 * Inspiration: https://rigtorp.se/spinlock/
 */

inline bool vlock_unlocked_old(vlock *vlock, uint64_t ts)
{
    uint64_t snapshot = atomic_load(vlock);
    return unlocked(snapshot) && (getversion(snapshot) <= ts);
}

inline bool vlock_release_explicitly_unlocked(vlock *vlock)
{
    uint64_t _vlock = atomic_load(vlock);
    uint64_t expected = _vlock | ((uint64_t)1 << 63);
    uint64_t desired = getversion(_vlock);
    if (atomic_compare_exchange_strong(vlock, &expected, desired))
    {
        return true;
    }
    fprintf(stderr, "unlocked already unlocked vlock\n");
    traceerror();
    return false;
}

inline bool vlock_release(vlock *vlock)
{
    uint64_t _vlock = atomic_load(vlock);
    uint64_t desired = getversion(_vlock);
    atomic_store_explicit(vlock, desired, memory_order_release);
    return true;
}

inline bool vlock_bounded_spinlock_acquire(vlock *vlock)
{
    uint64_t _vlock = atomic_load(vlock);
    uint64_t desired = getversion(_vlock) | ((uint64_t)1 << 63);
    uint64_t expected;
    for (uint64_t i = 0; i < SPINLOCK_BOUND; i++)
    {
        expected = getversion(_vlock);
        // if (atomic_compare_exchange_strong(vlock, &expected, desired))
        // {
        //     return true;
        // }

        if (atomic_compare_exchange_strong_explicit(
                vlock, &expected, desired,
                memory_order_acquire,
                memory_order_acquire))
        {
            return true;
        }

        if (locked(atomic_load_explicit(vlock, memory_order_relaxed)))
        {
            __builtin_ia32_pause();
        }
    }
    return false;
}

inline void vlock_update(vlock *vlock, uint64_t version)
{
    uint64_t lock = getlock(atomic_load(vlock));
    atomic_store(vlock, lock | version);
}

inline bool bounded_spinlock_acquire(lock *lock)
{
    for (uint64_t i = 0; i < SPINLOCK_BOUND; i++)
    {
        bool _unlocked = false;
        if (atomic_compare_exchange_strong(lock, &_unlocked, LOCKED))
        {
            return true;
        }
    }
    return false;
}

inline bool lock_release(lock *lock)
{
    bool _locked = true;
    if (atomic_compare_exchange_strong(lock, &_locked, UNLOCKED))
    {
        return true;
    }
    fprintf(stderr, "attempted to unlock already unlocked lock\n");
    traceerror();
    return false;
}