#ifndef CACHE_H
#define CACHE_H
#include <stdbool.h>

struct cache {
    char *path;
};

int cache_open(struct cache *c, const char *path);
void cache_close(struct cache *c);
bool cache_is_fresh(struct cache *c, const char *path);
int cache_update(struct cache *c, const char *path);

#endif

