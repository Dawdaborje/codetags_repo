#define _GNU_SOURCE
#include "parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>

static const char *TAGS[] = {"NOTE","TODO","WARNING","WARN","FIXME","FIX","BUG"};
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
    if(cache_is_fresh(fc, path)) return 0;

    FILE *f=fopen(path,"r");
    if(!f) return 0;
    char **lines=NULL; size_t nlines=0, cap=0;
    char *line=NULL; size_t capln=0;
    while(getline(&line,&capln,f)>0){
        if(nlines==cap){ cap = cap?cap*2:256; lines=realloc(lines, cap*sizeof *lines); }
        lines[nlines++] = strdup(line);
    }
    free(line); fclose(f);

    int changed=0;
    for(size_t i=0;i<nlines;i++){
        char tag[16]={0}; char *content=NULL;
        if(!starts_with_prefix_and_tag(lines[i], tag, sizeof tag, &content)) continue;
        char idbuf[64]={0};
        if (!extract_id_token(lines[i], idbuf)){
            char canon_path[PATH_MAX];
            const char *ppath = path;
            if (realpath(path, canon_path)) ppath = canon_path;
            char *copy = strdup(content);
            size_t lc=strlen(copy);
            while(lc>0 && isspace((unsigned char)copy[lc-1])) copy[--lc]=0;
            char keybuf[8192];
            snprintf(keybuf, sizeof keybuf, "%s::%s::%s", ppath, tag, copy);
            free(copy);
            idmap_get_or_assign(map, keybuf, idbuf);
        }
        char *newline=NULL;
        char *orig = strdup(lines[i]);
        // strip any existing id before appending
        char *lb = strrchr(orig, '[');
        if(lb && strncmp(lb,"[CT-",4)==0){
            while(lb>orig && isspace((unsigned char)*(lb-1))) lb--;
            *lb = 0;
        }
        size_t leno = strlen(orig);
        while(leno>0 && isspace((unsigned char)orig[leno-1])) orig[--leno]=0;
        if (asprintf(&newline, "%s [%s]\n", orig, idbuf) >= 0){
            if(strcmp(lines[i], newline)!=0){
                free(lines[i]); lines[i]=newline; changed=1;
            } else {
                free(newline);
            }
        }
        free(orig);
    }

    if(changed){
        FILE *o=fopen(path,"w");
        if(o){
            for(size_t i=0;i<nlines;i++){
                fputs(lines[i], o);
                free(lines[i]);
            }
            fclose(o);
        } else {
            for(size_t i=0;i<nlines;i++) free(lines[i]);
        }
    } else {
        for(size_t i=0;i<nlines;i++) free(lines[i]);
    }
    free(lines);
    cache_update(fc, path);
    return changed;
}

