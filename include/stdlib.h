#ifndef ARCHFORGE_STDLIB_H
#define ARCHFORGE_STDLIB_H

#include <stddef.h>
#include <stdint.h>

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
int abs(int n);
long labs(long n);
long long llabs(long long n);

#define NULL ((void *)0)

#endif