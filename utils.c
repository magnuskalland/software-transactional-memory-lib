#include "utils.h"
#include <stdint.h>

#include "sync.h"
#include "macros.h"

bool in_set(array *array, void *ptr)
{
    for (uint64_t i = 0; i < array->size; i++)
    {
        if (arrayget(array, i) == ptr)
        {
            return true;
        }
    }
    return false;
}

bool release_vlocks(array *vlocks)
{
    bool err = true;
    for (uint64_t i = 0; i < vlocks->size; i++)
    {
        if (!vlock_release(arrayget(vlocks, i)))
        {
            err = false;
            traceerror();
        }
    }
    return err;
}

write_entry *in_write_set(array *set, const char *addr)
{
    for (uint64_t i = 0; i < set->size; i++)
    {
        if (addr == ((write_entry *)arrayget(set, i))->dest)
        {
            return (write_entry *)arrayget(set, i);
        }
    }
    return NULL;
}