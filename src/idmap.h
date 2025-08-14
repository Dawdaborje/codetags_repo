#ifndef IDMAP_H
#define IDMAP_H
#include <stdio.h>

struct idmap {
    FILE *mapf;
    char *map_path;
    char *lastid_path;
};

int idmap_open(struct idmap *m, const char *map_path, const char *lastid_path);
void idmap_close(struct idmap *m);
int idmap_get_or_assign(struct idmap *m, const char *key, char out_id[64]);

#endif

