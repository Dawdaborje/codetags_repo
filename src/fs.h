#ifndef FS_H
#define FS_H
#include <stdbool.h>
#include "ignore.h"

typedef struct {
    int type;
    char *path;
} fs_event;

enum {
    FS_EVENT_NONE=0,
    FS_EVENT_WRITE,
    FS_EVENT_CREATE_FILE,
    FS_EVENT_DELETE_FILE,
    FS_EVENT_MOVE,
    FS_EVENT_CREATE_DIR,
    FS_EVENT_DELETE_DIR
};

typedef struct {
    int inofd;
    struct wdmap { int wd; char *path; } *wds;
    int wds_len, wds_cap;
    char *root;
} fs_watch_context;

typedef int (*onfile_cb)(const char *path, struct ignore *ig, void *a, void *b, void *c);

int fs_walk_files(const char *root, struct ignore *ig, onfile_cb cb, void *a, void *b, void *c);
bool fs_should_parse_file(const char *path, struct ignore *ig);

int fs_watch_init(fs_watch_context *c, const char *root, struct ignore *ig);
int fs_watch_add_dir_recursive(fs_watch_context *c, const char *dir, struct ignore *ig);
int fs_watch_remove_dir(fs_watch_context *c, const char *dir);
int fs_watch_next(fs_watch_context *c, fs_event *ev);
void fs_event_free(fs_event *ev);
void fs_watch_close(fs_watch_context *c);

#endif

