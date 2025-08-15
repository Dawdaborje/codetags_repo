#define _GNU_SOURCE
#include "cache.h"
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>

static int canon_abs(const char *in, char out[PATH_MAX]){
    // Prefer realpath (resolves symlinks and normalizes)
    if (realpath(in, out)) return 0;
    // Fallback: build absolute from CWD
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd)) return -1;
    if (snprintf(out, PATH_MAX, "%s/%s", cwd, in) >= (int)PATH_MAX) return -1;
    return 0;
}

int cache_open(struct cache *c, const char *path){
    c->path = strdup(path);
    FILE *f = fopen(c->path, "a");
    if (f) fclose(f);
    return 0;
}

void cache_close(struct cache *c){
    free(c->path);
}

static int read_stat(const char *p, long *size, long *mtime){
    struct stat st;
    if (stat(p, &st) != 0) return -1;
    *size = (long)st.st_size;
    *mtime = (long)st.st_mtime;
    return 0;
}

bool cache_is_fresh(struct cache *c, const char *path){
    char apath[PATH_MAX];
    if (canon_abs(path, apath) != 0) return false;

    long size, mtime;
    if (read_stat(apath, &size, &mtime) != 0) return false;

    FILE *f = fopen(c->path, "r");
    if (!f) return false;

    char rp[PATH_MAX];
    long osize, omtime;
    int fresh = 0;

    while (fscanf(f, "%4095s %ld %ld\n", rp, &osize, &omtime) == 3) {
        if (strcmp(rp, apath) == 0) {
            if (osize == size && omtime == mtime) fresh = 1;
            break;
        }
    }
    fclose(f);
    return fresh;
}

int cache_update(struct cache *c, const char *path){
    char apath[PATH_MAX];
    if (canon_abs(path, apath) != 0) return -1;

    long size, mtime;
    if (read_stat(apath, &size, &mtime) != 0) return -1;

    FILE *in = fopen(c->path, "r");
    char *tmp = NULL;
    if (asprintf(&tmp, "%s.tmp", c->path) < 0) return -1;
    FILE *out = fopen(tmp, "w");
    if (!out) { free(tmp); return -1; }

    if (in) {
        char rp[PATH_MAX];
        long osize, omtime;
        int found = 0;
        while (fscanf(in, "%4095s %ld %ld\n", rp, &osize, &omtime) == 3) {
            if (strcmp(rp, apath) == 0) {
                fprintf(out, "%s %ld %ld\n", apath, size, mtime);
                found = 1;
            } else {
                fprintf(out, "%s %ld %ld\n", rp, osize, omtime);
            }
        }
        if (!found) fprintf(out, "%s %ld %ld\n", apath, size, mtime);
        fclose(in);
    } else {
        fprintf(out, "%s %ld %ld\n", apath, size, mtime);
    }

    fclose(out);
    rename(tmp, c->path);
    free(tmp);
    return 0;
}

