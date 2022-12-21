#ifndef ARRAY_H
#define ARRAY_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define INITIAL_SIZE 16
#define arrayget(a, i) a->array[i]

typedef struct array
{
    uint64_t size;
    uint64_t max_size;
    void **array;
} array;

void *array_init();
void *array_init_size(uint64_t init_size);
void array_destroy(array *a);
void array_add(array **a, void *element);
void *array_get(array *a, uint64_t index);
void array_destroy(array *a);

#endif