#define _GNU_SOURCE
#include "parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>

//NOTE: hello world [CT-1-76a9538c]
//TODO: Add more tags [CT-2-89272b86]
static const char *TAGS[] = {"NOTE","TODO","WARNING","WARN","FIXME","FIX","BUG"};
//TODO: Add more programming langs to mamke it more comprehensive. [CT-3-140e99a2]
static const char *PFX[]  = {"#","//",";","--","%"};

static int extract_id_token(const char *line, char out[64]){
    const char *lb = strrchr(line, '[');
    const char *rb = lb ? strchr(lb, ']') : NULL;
    if (!lb || !rb || rb < lb) return 0;
    if (strncmp(lb, "[CT-", 4) != 0) return 0;
    for (const char *p = rb+1; *p; ++p) if (!isspace((unsigned char)*p)) return 0;
    size_t m = (size_t)(rb - (lb+1));
    if (m >= 63) m = 63;
    memcpy(out, lb+1, m);
    out[m] = 0;
    return 1;
}

static int starts_with_prefix_and_tag(const char *line, char *tag_out, size_t tag_out_sz, char **content_out){
    const char *s=line;
    while(isspace((unsigned char)*s)) s++;
    for(size_t i=0;i<sizeof(PFX)/sizeof(PFX[0]);i++){
        size_t l=strlen(PFX[i]);
        if(strncmp(s,PFX[i],l)==0){
            s+=l;
            while(isspace((unsigned char)*s)) s++;
            for(size_t t=0;t<sizeof(TAGS)/sizeof(TAGS[0]);t++){
                size_t tl=strlen(TAGS[t]);
                if(strncasecmp(s,TAGS[t],tl)==0 && s[tl]==':'){
                    strncpy(tag_out,TAGS[t],tag_out_sz-1);
                    tag_out[tag_out_sz-1]=0;
                    s += tl+1;
                    while(isspace((unsigned char)*s)) s++;
                    *content_out=(char*)s;
                    return 1;
                }
            }
        }
    }
    return 0;
}



int parse_file_inplace(const char *path, struct idmap *map, struct cache *fc){
    int may_skip = cache_is_fresh(fc, path);
    if (may_skip) {
        FILE *tf = fopen(path, "r");
        if (tf) {
            int has_ct = 0;
            char *l = NULL; size_t cap = 0;
            while (getline(&l, &cap, tf) > 0) {
                if (strstr(l, "[CT-")) { has_ct = 1; break; }
            }
            free(l);
            fclose(tf);
            if (!has_ct) {
                may_skip = 0; 
            }
        } else {
            may_skip = 0; 

        }
    }
    if (may_skip) return 0;

    char apath[PATH_MAX];
    const char *opath = path;
    if (realpath(path, apath)) opath = apath;

    FILE *f = fopen(opath, "r");
    if (!f) return 0;

    char **lines = NULL; size_t nlines = 0, cap = 0;
    char *line = NULL; size_t capln = 0;
    while (getline(&line, &capln, f) > 0) {
        if (nlines == cap) {
            cap = cap ? cap*2 : 256;
            lines = realloc(lines, cap * sizeof *lines);
        }
        lines[nlines++] = strdup(line);
    }
    free(line);
    fclose(f);

    int changed = 0;
    for (size_t i = 0; i < nlines; i++) {
        char tag[16] = {0}; char *content = NULL;
        if (!starts_with_prefix_and_tag(lines[i], tag, sizeof tag, &content)) continue;

        // Check if line already has an ID token (legacy or current)
        char idbuf[64] = {0};
        int has_existing_id = extract_id_token(lines[i], idbuf);

        char canon_path[PATH_MAX];
        const char *ppath = opath;
        if (realpath(opath, canon_path)) ppath = canon_path;

        char *clean = strdup(content);
        if (!clean) continue;
        {
            char *lb = strrchr(clean, '[');
            if (lb && strncmp(lb, "[CT-", 4) == 0) {
                while (lb > clean && isspace((unsigned char)*(lb-1))) lb--;
                *lb = 0;
            }
            size_t lc = strlen(clean);
            while (lc > 0 && isspace((unsigned char)clean[lc-1])) clean[--lc] = 0;
        }

        char keybuf[PATH_MAX + 256];
        snprintf(keybuf, sizeof keybuf, "%s::%s::%s", ppath, tag, clean);

        if (has_existing_id) {
            if (idmap_ensure_mapping(map, keybuf, idbuf) != 0) {
                // If ensure failed, proceed without changing the line
            }
        } else {
            if (idmap_get_or_assign(map, keybuf, idbuf) == 0) {
                char *newline = NULL;
                char *orig = strdup(lines[i]);
                if (orig) {
                    char *lb2 = strrchr(orig, '[');
                    if (lb2 && strncmp(lb2, "[CT-", 4) == 0) {
                        while (lb2 > orig && isspace((unsigned char)*(lb2-1))) lb2--;
                        *lb2 = 0;
                    }
                    size_t leno = strlen(orig);
                    while (leno > 0 && isspace((unsigned char)orig[leno-1])) orig[--leno] = 0;

                    if (asprintf(&newline, "%s [%s]\n", orig, idbuf) >= 0) {
                        if (strcmp(lines[i], newline) != 0) {
                            free(lines[i]); lines[i] = newline; changed = 1;
                        } else {
                            free(newline);
                        }
                    }
                    free(orig);
                }
            }
        }

        free(clean);
    }

    if (changed) {
        FILE *o = fopen(opath, "w");
        if (o) {
            for (size_t i = 0; i < nlines; i++) {
                fputs(lines[i], o);
                free(lines[i]);
            }
            fclose(o);
        } else {
            for (size_t i = 0; i < nlines; i++) free(lines[i]);
        }
    } else {
        for (size_t i = 0; i < nlines; i++) free(lines[i]);
    }
    free(lines);

    cache_update(fc, opath);
    return changed;
}

