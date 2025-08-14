#define _GNU_SOURCE
#include "fs.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <limits.h>
#include <errno.h>

static char *strdup0(const char *s){
    if(!s) return NULL;
    size_t n=strlen(s);
    char *d=malloc(n+1);
    if(d) memcpy(d,s,n+1);
    return d;
}

static int is_dir(const char *p){
    struct stat st; if(stat(p,&st)!=0) return 0;
    return S_ISDIR(st.st_mode);
}
static int is_reg(const char *p){
    struct stat st; if(stat(p,&st)!=0) return 0;
    return S_ISREG(st.st_mode);
}

static char *relpath_from_root(const char *root, const char *abs){
    size_t rl=strlen(root);
    if(strncmp(root, abs, rl)==0){
        const char *p=abs+rl;
        if(*p=='/') p++;
        return strdup0(p);
    } else {
        return strdup0(abs);
    }
}

bool fs_should_parse_file(const char *path, struct ignore *ig){
    if(!is_reg(path)) return false;
    const char *ext=strrchr(path,'.');
    if(ext && (!strcmp(ext,".png")||!strcmp(ext,".jpg")||!strcmp(ext,".jpeg")||
               !strcmp(ext,".gif")||!strcmp(ext,".pdf")||!strcmp(ext,".zip")||
               !strcmp(ext,".gz")||!strcmp(ext,".tar")||!strcmp(ext,".jar")||
               !strcmp(ext,".war")||!strcmp(ext,".class")||!strcmp(ext,".exe")||
               !strcmp(ext,".so")||!strcmp(ext,".o")||!strcmp(ext,".a")))
        return false;
    if(strstr(path, "/.ctags/")!=NULL || strcmp(path,".ctags")==0) return false;
    char cwd[PATH_MAX];
    if (!getcwd(cwd,sizeof cwd)) return false;
    char abspath[PATH_MAX];
    if (!realpath(path, abspath)) return false;
    char *rel = relpath_from_root(cwd, abspath);
    bool ignored = ignore_match(ig, rel, false);
    free(rel);
    return !ignored;
}

int fs_walk_files(const char *root, struct ignore *ig, onfile_cb cb, void *a, void *b, void *c){
    DIR *d=opendir(root);
    if(!d) return -1;
    struct dirent *e;
    while((e=readdir(d))){
        if(!strcmp(e->d_name,".")||!strcmp(e->d_name,"..")) continue;
        char *p=NULL; if (asprintf(&p, "%s/%s", root, e->d_name) < 0) continue;
        int isdir = is_dir(p);
        char cwd[PATH_MAX];
        if (!getcwd(cwd,sizeof cwd)){ free(p); continue; }
        char abspath[PATH_MAX];
        if (!realpath(p, abspath)){ free(p); continue; }
        char *rel = relpath_from_root(cwd, abspath);
        int ignored = ignore_match(ig, rel, isdir);
        free(rel);
        if(ignored){ free(p); continue; }
        if(isdir){
            if(strcmp(e->d_name,".ctags")==0){ free(p); continue; }
            fs_walk_files(p, ig, cb, a, b, c);
        } else {
            if(fs_should_parse_file(p, ig)) cb(p, ig, a, b, c);
        }
        free(p);
    }
    closedir(d);
    return 0;
}

static void wdmap_add(fs_watch_context *c, int wd, const char *path){
    if(c->wds_len==c->wds_cap){
        c->wds_cap = c->wds_cap? c->wds_cap*2 : 64;
        c->wds = realloc(c->wds, c->wds_cap*sizeof *c->wds);
    }
    c->wds[c->wds_len].wd = wd;
    c->wds[c->wds_len].path = strdup0(path);
    c->wds_len++;
}

static char *wd_path(fs_watch_context *c, int wd){
    for(int i=0;i<c->wds_len;i++) if(c->wds[i].wd==wd) return c->wds[i].path;
    return NULL;
}

int fs_watch_init(fs_watch_context *c, const char *root, struct ignore *ig){
    memset(c,0,sizeof *c);
    c->root = realpath(root, NULL);
    c->inofd = inotify_init1(IN_NONBLOCK);
    if(c->inofd<0) return -1;
    return fs_watch_add_dir_recursive(c, root, ig);
}

static int add_watch_dir(fs_watch_context *c, const char *dir){
    int mask = IN_CREATE|IN_DELETE|IN_MOVED_FROM|IN_MOVED_TO|
               IN_MODIFY|IN_CLOSE_WRITE|IN_DELETE_SELF|IN_MOVE_SELF;
    int wd = inotify_add_watch(c->inofd, dir, mask);
    if(wd<0) return -1;
    wdmap_add(c, wd, dir);
    return 0;
}

int fs_watch_add_dir_recursive(fs_watch_context *c, const char *dir, struct ignore *ig){
    add_watch_dir(c, dir);
    DIR *d=opendir(dir); if(!d) return 0;
    struct dirent *e;
    while((e=readdir(d))){
        if(!strcmp(e->d_name,".")||!strcmp(e->d_name,"..")) continue;
        char *p=NULL; if (asprintf(&p, "%s/%s", dir, e->d_name) < 0) continue;
        if(is_dir(p)){
            char cwd[PATH_MAX];
            if (!getcwd(cwd,sizeof cwd)){ free(p); continue; }
            char abspath[PATH_MAX];
            if (!realpath(p, abspath)){ free(p); continue; }
            char *rel = relpath_from_root(cwd, abspath);
            int ignored = ignore_match(ig, rel, true);
            free(rel);
            if(ignored || strstr(p,"/.ctags/")!=NULL || strcmp(e->d_name,".ctags")==0){ free(p); continue; }
            fs_watch_add_dir_recursive(c, p, ig);
        }
        free(p);
    }
    closedir(d);
    return 0;
}

int fs_watch_remove_dir(fs_watch_context *c, const char *dir){
    (void)c; (void)dir;
    return 0;
}

int fs_watch_next(fs_watch_context *c, fs_event *ev){
    char buf[64*1024] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    ssize_t len = read(c->inofd, buf, sizeof buf);
    if(len<0){
        if(errno==EAGAIN){ usleep(100000); return 0; }
        return -1;
    }
    size_t i=0;
    while(i < (size_t)len){
        struct inotify_event *ie = (struct inotify_event *)(buf + i);
        i += sizeof(*ie) + ie->len;
        char *dpath = wd_path(c, ie->wd);
        if(!dpath) continue;
        char *path=NULL;
        if(ie->len && ie->name[0]){
            if (asprintf(&path, "%s/%s", dpath, ie->name) < 0) path=NULL;
        } else {
            path = strdup0(dpath);
        }
        ev->path = path;
        if(ie->mask & IN_ISDIR){
            if(ie->mask & (IN_CREATE|IN_MOVED_TO)){ ev->type = FS_EVENT_CREATE_DIR; return 1; }
            if(ie->mask & (IN_DELETE|IN_MOVED_FROM|IN_DELETE_SELF)){ ev->type = FS_EVENT_DELETE_DIR; return 1; }
        } else {
            if(ie->mask & (IN_CLOSE_WRITE|IN_MODIFY)){ ev->type = FS_EVENT_WRITE; return 1; }
            if(ie->mask & (IN_CREATE|IN_MOVED_TO)){ ev->type = FS_EVENT_CREATE_FILE; return 1; }
            if(ie->mask & (IN_DELETE|IN_MOVED_FROM)){ ev->type = FS_EVENT_DELETE_FILE; return 1; }
        }
        ev->type = FS_EVENT_MOVE;
        return 1;
    }
    return 0;
}

void fs_event_free(fs_event *ev){
    free(ev->path); ev->path=NULL; ev->type=FS_EVENT_NONE;
}

void fs_watch_close(fs_watch_context *c){
    for(int i=0;i<c->wds_len;i++){
        inotify_rm_watch(c->inofd, c->wds[i].wd);
        free(c->wds[i].path);
    }
    free(c->wds);
    if(c->inofd>=0) close(c->inofd);
    free(c->root);
}

