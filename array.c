#include "array.h"

#include <string.h>
#include <stdio.h>

inline void *array_init()
{
    array *a;

    a = malloc(sizeof(struct array));
    if (!a)
    {
        perror("malloc");
        return NULL;
    }

    a->size = 0;
    a->max_size = INITIAL_SIZE;

    a->array = calloc(sizeof(void *), a->max_size);
    if (!a->array)
    {
        perror("malloc");
        return NULL;
    }

    return a;
}

inline void *array_init_size(uint64_t init_size)
{
    array *a;

    a = malloc(sizeof(struct array));
    if (!a)
    {
        perror("malloc");
        return NULL;
    }

    a->size = 0;
    a->max_size = init_size;

    a->array = calloc(sizeof(void *), a->max_size);
    if (!a->array)
    {
        perror("malloc");
        return NULL;
    }

    return a;
}

inline void array_add(array **a, void *element)
{
    if ((*a)->size == (*a)->max_size)
    {
        (*a)->array = realloc((*a)->array, sizeof(void *) * (*a)->size * 2);
        (*a)->max_size = (*a)->size * 2;
    }
    (*a)->array[(*a)->size] = element;
    (*a)->size += 1;
}

inline void *array_get(array *a, uint64_t index)
{
    if (index > a->size - 1)
    {
        fprintf(stderr, "accessing element %ld of array of size %ld\n", index, a->size);
        return NULL;
    }
    return a->array[index];
}

inline void array_destroy(array *a)
{
    free(a->array);
    free(a);
}