#define _GNU_SOURCE
#include "idmap.h"
#include "hash.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

static long read_last(const char *p){
    FILE *f=fopen(p,"r");
    if(!f) return 0;
    long v=0; if(fscanf(f,"%ld",&v)!=1) v=0; fclose(f); return v;
}

static int write_last(const char *p, long v){
    FILE *f=fopen(p,"w"); if(!f) return -1;
    fprintf(f,"%ld\n",v); fclose(f); return 0;
}

static int urand32(uint32_t *out){
    FILE *r = fopen("/dev/urandom","rb");
    if(!r) return -1;
    size_t n=fread(out,1,sizeof *out,r);
    fclose(r);
    return n==sizeof *out ? 0 : -1;
}

int idmap_open(struct idmap *m, const char *map_path, const char *lastid_path){
    m->map_path = strdup(map_path);
    m->lastid_path = strdup(lastid_path);
    m->mapf = fopen(map_path, "a+");
    if(!m->mapf) return -1;
    return 0;
}

void idmap_close(struct idmap *m){
    if(m->mapf) fclose(m->mapf);
    free(m->map_path); free(m->lastid_path);
}

int idmap_get_or_assign(struct idmap *m, const char *key, char out_id[64]){
    fflush(m->mapf);
    FILE *f = fopen(m->map_path, "r");
    if(f){
        char *line=NULL; size_t cap=0;
        while(getline(&line,&cap,f)>0){
            char *tab=strchr(line,'\t');
            if(!tab) continue;
            *tab=0;
            if(strcmp(line,key)==0){
                char *id=tab+1; size_t n=strlen(id);
                while(n>0 && (id[n-1]=='\n' || id[n-1]=='\r')) id[--n]=0;
                strncpy(out_id,id,63); out_id[63]=0;
                free(line); fclose(f); return 0;
            }
        }
        free(line); fclose(f);
    }
    long next = read_last(m->lastid_path) + 1;
    write_last(m->lastid_path, next);
    uint32_t rnd;
    if (urand32(&rnd) != 0) {
        uint64_t h = fnv1a64(key, strlen(key));
        rnd = (uint32_t)(h ^ (uint64_t)next);
    }
    snprintf(out_id, 64, "CT-%ld-%08x", next, rnd);
    m->mapf = fopen(m->map_path, "a");
    if(!m->mapf) return -1;
    fprintf(m->mapf, "%s\t%s\n", key, out_id);
    fclose(m->mapf);
    m->mapf = fopen(m->map_path, "a+");
    return 0;
}


int idmap_ensure_mapping(struct idmap *m, const char *key, const char *id){
    // Return 0 if mapping exists or was created, -1 on error
    fflush(m->mapf);
    // Check existing
    FILE *f = fopen(m->map_path, "r");
    if (f) {
        char *line = NULL; size_t cap = 0; int found = 0;
        while (getline(&line, &cap, f) > 0) {
            char *tab = strchr(line, '\t');
            if (!tab) continue;
            *tab = 0;
            if (strcmp(line, key) == 0) { found = 1; break; }
        }
        free(line);
        fclose(f);
        if (found) return 0;
    }
    // Append mapping
    FILE *a = fopen(m->map_path, "a");
    if (!a) return -1;
    fprintf(a, "%s\t%s\n", key, id);
    fclose(a);
    // Reopen mapf in append+read mode for future ops
    if (m->mapf) fclose(m->mapf);
    m->mapf = fopen(m->map_path, "a+");
    return m->mapf ? 0 : -1;
}
