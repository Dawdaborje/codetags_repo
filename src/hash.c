#include "hash.h"
#include <stddef.h>

uint64_t fnv1a64(const void *data, size_t len){
    const unsigned char *p = (const unsigned char*)data;
    uint64_t h = 1469598103934665603ULL;
    while(len--) {
        h ^= *p++;
        h *= 1099511628211ULL;
    }
    return h;
}

