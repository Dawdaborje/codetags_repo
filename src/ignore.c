#define _GNU_SOURCE
#include "ignore.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static char *strdup0(const char *s){
    if(!s) return NULL;
    size_t n=strlen(s);
    char *d=malloc(n+1);
    if(d) memcpy(d,s,n+1);
    return d;
}

static void trim(char *s){
    char *p=s; while(isspace((unsigned char)*p)) p++;
    if(p!=s) memmove(s,p,strlen(s)+1);
    size_t n=strlen(s);
    while(n>0 && isspace((unsigned char)s[n-1])) s[--n]=0;
}

static int parse_line(const char *line, struct ignore_rule **out){
    char *buf=strdup0(line);
    trim(buf);
    if(buf[0]==0 || buf[0]=='#'){ free(buf); return 0; }
    struct ignore_rule *r=calloc(1,sizeof *r);
    if(buf[0]=='!'){ r->neg=true; memmove(buf,buf+1,strlen(buf)); }
    size_t n=strlen(buf);
    if(n>0 && buf[n-1]=='/'){ r->only_dir=true; buf[n-1]=0; }
    if(buf[0]=='/'){ r->anchored=true; memmove(buf,buf+1,strlen(buf)); }
    r->pattern=buf;
    *out=r;
    return 1;
}

int ignore_load(struct ignore *ig, const char *file){
    ig->rules=NULL;
    FILE *f=fopen(file,"r");
    if(!f) return 0;
    char *line=NULL; size_t cap=0;
    while(getline(&line,&cap,f)>0){
        struct ignore_rule *r=NULL;
        if(parse_line(line,&r)>0){
            r->next=ig->rules;
            ig->rules=r;
        }
    }
    free(line);
    fclose(f);
    return 0;
}

static bool match_glob_segment(const char *name, const char *pat){
    const char *n=name, *p=pat, *star=NULL, *np=NULL;
    while(*n){
        if(*p=='*'){ star=p++; np=n; }
        else if(*p=='?' || *p==*n){ p++; n++; }
        else if(star){ p=star+1; n=++np; }
        else return false;
    }
    while(*p=='*') p++;
    return *p==0;
}

static bool match_double_star(const char *path, const char *pat){
    if(!*pat) return !*path;
    if(pat[0]=='*' && pat[1]=='*'){
        pat+=2;
        if(*pat=='/') { pat++; }
        const char *p=path;
        do {
            if(match_double_star(p, pat)) return true;
            while(*p && *p!='/') p++;
            if(*p=='/') p++;
        } while(*p);
        return match_double_star(p, pat);
    } else {
        const char *pslash=strchr(pat,'/');
        const char *nslash=strchr(path,'/');
        size_t plen = pslash ? (size_t)(pslash - pat) : strlen(pat);
        size_t nlen = nslash ? (size_t)(nslash - path) : strlen(path);
        char pb[512], nb[512];
        if(plen>=sizeof pb || nlen>=sizeof nb) return false;
        memcpy(pb, pat, plen); pb[plen]=0;
        memcpy(nb, path, nlen); nb[nlen]=0;
        if(!match_glob_segment(nb, pb)) return false;
        if(pslash) {
            if(!nslash) return false;
            return match_double_star(nslash+1, pslash+1);
        } else {
            return nslash==NULL;
        }
    }
}

static bool pattern_match_path(const char *relpath, const char *pat, bool anchored){
    if(anchored) {
        return match_double_star(relpath, pat);
    }
    const char *p=relpath;
    do {
        if(match_double_star(p, pat)) return true;
        p = strchr(p, '/');
        if(p) p++;
    } while(p);
    return false;
}

bool ignore_match(const struct ignore *ig, const char *relpath, bool is_dir){
    for(const struct ignore_rule *r=ig->rules; r; r=r->next){
        if(r->only_dir && !is_dir) continue;
        if(pattern_match_path(relpath, r->pattern, r->anchored)){
            return !r->neg;
        }
    }
    return false;
}

void ignore_free(struct ignore *ig){
    struct ignore_rule *r=ig->rules;
    while(r){
        struct ignore_rule *n=r->next;
        free(r->pattern);
        free(r);
        r=n;
    }
    ig->rules=NULL;
}

