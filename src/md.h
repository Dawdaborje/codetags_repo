#ifndef MD_H
#define MD_H
#include "idmap.h"

int md_initialize(const char *mdpath);
int md_rebuild(const char *mdpath, struct idmap *map);

#endif

