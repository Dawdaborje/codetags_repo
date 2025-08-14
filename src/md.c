#define _GNU_SOURCE
#include "md.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <ctype.h>

static const char *SECS[] = {"NOTE","TODO","WARNING","WARN","FIXME","FIX","BUG"};

int md_initialize(const char *mdpath){
    FILE *f = fopen(mdpath,"w");
    if(!f) return -1;
    fprintf(f,"# Codetags\n\n");
    for(size_t i=0; i<sizeof(SECS)/sizeof(SECS[0]); i++){
        fprintf(f,"## %s\n\n_No entries yet._\n\n", SECS[i]);
    }
    fclose(f);
    return 0;
}

static void write_section(FILE *out, const char *tag, FILE *mapf){
    rewind(mapf);
    char *line = NULL; size_t cap = 0;
    int wrote_any = 0;
    char cwd[PATH_MAX];
    if (!getcwd(cwd,sizeof cwd)) cwd[0] = 0;

    while(getline(&line,&cap,mapf) > 0){
        char *tab = strchr(line,'\t');
        if(!tab) continue;
        *tab = 0;
        char *key = line;
        char *id = tab+1;
        size_t n = strlen(id);
        while(n>0 && (id[n-1]=='\n' || id[n-1]=='\r')) id[--n] = 0;

        // key format: path::tag::content
        char *p1 = strstr(key,"::"); if(!p1) continue; *p1 = 0; char *path = key;
        char *p2 = strstr(p1+2,"::"); if(!p2) continue; *p2 = 0; char *ktag = p1+2;

        if(strcmp(ktag, tag) != 0) continue;

        FILE *ff = fopen(path,"r");
        if(!ff) continue;

        char *fl = NULL; size_t fcap = 0; long cur = 0; int found = 0;
        char display[4096] = {0};
        while(getline(&fl,&fcap,ff) > 0){
            cur++;
            if(strstr(fl, id)){
                char *pos = (char*)strcasestr(fl, tag);
                if(pos){
                    pos = strchr(pos, ':');
                    if(pos){
                        pos++;
                        while(*pos && isspace((unsigned char)*pos)) pos++;
                    }
                    // Strip trailing [CT-...] if present
                    char *lb = strrchr(pos,'[');
                    if(lb && strncmp(lb,"[CT-",4) == 0){
                        char *q = lb;
                        while(q > pos && isspace((unsigned char)*(q-1))) q--;
                        *q = 0;
                    }
                    // Trim newline and CR
                    size_t L = strlen(pos);
                    while(L > 0 && (pos[L-1] == '\n' || pos[L-1] == '\r')){
                        pos[--L] = 0;
                    }
                    strncpy(display,pos,sizeof display - 1);
                }
                const char *out_path = path;
                if(cwd[0] && strncmp(path,cwd,strlen(cwd)) == 0){
                    const char *rp = path + strlen(cwd);
                    if(*rp == '/') rp++;
                    out_path = rp;
                }
                fprintf(out, "- [%s] %s:%ld — %s\n", id, out_path, cur, display);
                wrote_any = 1;
                found = 1;
                break;
            }
        }
        free(fl);
        fclose(ff);
        if(!found) continue;
    }
    free(line);
    if(!wrote_any) fprintf(out, "_No entries yet._\n");
    fprintf(out,"\n");
}

int md_rebuild(const char *mdpath, struct idmap *map){
    FILE *mf = fopen(map->map_path, "r");
    if(!mf) return -1;
    FILE *out = fopen(mdpath,"w");
    if(!out){
        fclose(mf);
        return -1;
    }
    fprintf(out,"# Codetags\n\n");
    for(size_t i=0; i<sizeof(SECS)/sizeof(SECS[0]); i++){
        fprintf(out,"## %s\n\n", SECS[i]);
        write_section(out, SECS[i], mf);
    }
    fclose(out);
    fclose(mf);
    return 0;
}

