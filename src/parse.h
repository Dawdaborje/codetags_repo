#ifndef PARSE_H
#define PARSE_H
#include "idmap.h"
#include "cache.h"

int parse_file_inplace(const char *path, struct idmap *map, struct cache *fc);

#endif

