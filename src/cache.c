#define _GNU_SOURCE
#include "cache.h"
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int cache_open(struct cache *c, const char *path){
    c->path=strdup(path);
    FILE *f=fopen(c->path,"a"); if(f) fclose(f);
    return 0;
}
void cache_close(struct cache *c){
    free(c->path);
}

static int read_stat(const char *p, long *size, long *mtime){
    struct stat st; if(stat(p,&st)!=0) return -1;
    *size = (long)st.st_size; *mtime = (long)st.st_mtime;
    return 0;
}

bool cache_is_fresh(struct cache *c, const char *path){
    long size, mtime;
    if(read_stat(path,&size,&mtime)!=0) return false;
    FILE *f=fopen(c->path,"r"); if(!f) return false;
    char rp[4096]; long osize, omtime; int fresh=0;
    while(fscanf(f, "%4095s %ld %ld\n", rp, &osize, &omtime)==3){
        if(strcmp(rp,path)==0){
            if(osize==size && omtime==mtime) fresh=1;
            break;
        }
    }
    fclose(f);
    return fresh;
}

int cache_update(struct cache *c, const char *path){
    long size, mtime;
    if(read_stat(path,&size,&mtime)!=0) return -1;
    FILE *in=fopen(c->path,"r");
    char *tmp=NULL; if (asprintf(&tmp,"%s.tmp", c->path) < 0) return -1;
    FILE *out=fopen(tmp,"w");
    if(in){
        char rp[4096]; long osize, omtime;
        int found=0;
        while(fscanf(in, "%4095s %ld %ld\n", rp, &osize, &omtime)==3){
            if(strcmp(rp,path)==0){
                fprintf(out, "%s %ld %ld\n", path, size, mtime);
                found=1;
            } else {
                fprintf(out, "%s %ld %ld\n", rp, osize, omtime);
            }
        }
        if(!found) fprintf(out, "%s %ld %ld\n", path, size, mtime);
        fclose(in);
    } else {
        fprintf(out, "%s %ld %ld\n", path, size, mtime);
    }
    fclose(out);
    rename(tmp, c->path);
    free(tmp);
    return 0;
}

