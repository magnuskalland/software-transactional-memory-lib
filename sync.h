#ifndef SYNC_H
#define SYNC_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define SPINLOCK_BOUND 100

#define LOCKED true
#define UNLOCKED false

/* versioned lock */
typedef atomic_ulong vlock;

#define unlocked(vlock) (getlock(vlock) >> 63 == (uint64_t)0)
#define getlock(l) (l & ((uint64_t)1 << 63))
#define getversion(v) (uint64_t)(v & (((uint64_t)1 << 63) - 1))

bool vlock_unlocked_old(vlock *vlock, uint64_t ts);
bool vlock_release(vlock *vlock);
bool vlock_bounded_spinlock_acquire(vlock *vlock);
void vlock_update(vlock *vlock, uint64_t version);

/* traditional lock */
typedef atomic_bool lock;
bool bounded_spinlock_acquire(lock *lock);
bool lock_release(lock *lock);

#endif