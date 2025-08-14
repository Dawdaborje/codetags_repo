#ifndef HASH_H
#define HASH_H
#include <stdint.h>
#include <stddef.h>

uint64_t fnv1a64(const void *data, size_t len);
#endif

