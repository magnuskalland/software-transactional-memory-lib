#ifndef UTILS_H
#define UTILS_H

#include "array.h"
#include "handler.h"
bool in_set(array *array, void *ptr);
bool release_vlocks(array *vlocks);
write_entry *in_write_set(array *set, const char *addr);

#endif
