#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <sys/inotify.h>
#include "fs.h"
#include "ignore.h"
#include "parse.h"
#include "md.h"
#include "idmap.h"
#include "cache.h"

#define REPO_DIR ".ctags"
#define STATE_DIR ".ctags/.state"
#define MAP_PATH ".ctags/.state/id_map.tsv"
#define LASTID_PATH ".ctags/.state/last_id.txt"
#define FILECACHE_PATH ".ctags/.state/filecache.tsv"
#define MD_PATH "codetags.md"

#define GLOBAL_DIR ".ctags"
#define GLOBAL_REGISTRY "registered_repos.txt"

static void usage(void) {
    puts(
        "codetags - parse and catalog codetags across repositories\n"
        "Usage:\n"
        "  codetags init\n"
        "  codetags scan <path>\n"
        "  codetags reindex\n"
        "  codetags watch   (system-wide; watches all registered repos)\n"
    );
}

static int ensure_repo_workspace(void) {
    if (mkdir(REPO_DIR, 0777) && errno != EEXIST) return -1;
    if (mkdir(STATE_DIR, 0777) && errno != EEXIST) return -1;
    FILE *f;
    f = fopen(MAP_PATH, "a"); if (!f) return -1; fclose(f);
    f = fopen(LASTID_PATH, "a"); if (!f) return -1; fclose(f);
    f = fopen(FILECACHE_PATH, "a"); if (!f) return -1; fclose(f);
    struct stat st;
    if (stat(MD_PATH, &st) != 0) {
        if (md_initialize(MD_PATH) != 0) {
            fprintf(stderr, "Failed to initialize %s\n", MD_PATH);
            return -1;
        }
    }
    return 0;
}

static int get_home(char home[PATH_MAX]) {
    const char *h = getenv("HOME");
    if (!h || !*h) return -1;
    if (strlen(h) >= PATH_MAX) return -1;
    strcpy(home, h);
    return 0;
}

static int ensure_global_dirs(char out_path[PATH_MAX]) {
    char home[PATH_MAX];
    if (get_home(home) != 0) return -1;
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s/%s", home, GLOBAL_DIR);
    if (mkdir(dir, 0777) && errno != EEXIST) return -1;
    snprintf(out_path, PATH_MAX, "%s/%s/%s", home, GLOBAL_DIR, GLOBAL_REGISTRY);
    FILE *f = fopen(out_path, "a");
    if (!f) return -1;
    fclose(f);
    return 0;
}

static int repo_root_path(char out[PATH_MAX]) {
    if (!getcwd(out, PATH_MAX)) return -1;
    return 0;
}

static int registry_contains(const char *registry, const char *repo) {
    FILE *f = fopen(registry, "r");
    if (!f) return 0;
    char *line = NULL; size_t cap = 0; int found = 0;
    while (getline(&line, &cap, f) > 0) {
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
        if (strcmp(line, repo) == 0) { found = 1; break; }
    }
    free(line);
    fclose(f);
    return found;
}

static int registry_add_repo(const char *repo) {
    char reg[PATH_MAX];
    if (ensure_global_dirs(reg) != 0) return -1;
    if (registry_contains(reg, repo)) return 0;
    FILE *f = fopen(reg, "a");
    if (!f) return -1;
    fprintf(f, "%s\n", repo);
    fclose(f);
    return 0;
}

static int cmd_init(void) {
    if (ensure_repo_workspace() != 0) {
        perror("init");
        return 1;
    }
    char root[PATH_MAX];
    if (repo_root_path(root) != 0) {
        fprintf(stderr, "Failed to get repo root\n");
        return 1;
    }
    if (registry_add_repo(root) != 0) {
        fprintf(stderr, "Warning: failed to register repo globally\n");
    }
    puts("codetags initialized.");
    return 0;
}

static int onfile_parse_repo(const char *path, struct ignore *ig, void *a, void *b, void *c) {
    (void)ig; (void)a;
    return parse_file_inplace(path, (struct idmap*)b, (struct cache*)c);
}

static int cmd_scan(const char *root) {
    if (ensure_repo_workspace() != 0) { perror("scan"); return 1; }
    struct ignore ig = {0};
    ignore_load(&ig, ".ctagsignore");
    struct idmap map = {0};
    if (idmap_open(&map, MAP_PATH, LASTID_PATH) != 0) {
        fprintf(stderr, "Failed to open id map\n"); return 1;
    }
    struct cache fc = {0};
    cache_open(&fc, FILECACHE_PATH);

    int rc = fs_walk_files(root, &ig, onfile_parse_repo, NULL, &map, &fc);
    if (rc != 0) fprintf(stderr, "Walk errors encountered\n");
    md_rebuild(MD_PATH, &map);
    idmap_close(&map);
    cache_close(&fc);
    ignore_free(&ig);
    return 0;
}

static int cmd_reindex(void) {
    if (ensure_repo_workspace() != 0) { perror("reindex"); return 1; }
    struct idmap map = {0};
    if (idmap_open(&map, MAP_PATH, LASTID_PATH) != 0) {
        fprintf(stderr, "Failed to open id map\n"); return 1;
    }
    md_rebuild(MD_PATH, &map);
    idmap_close(&map);
    puts("Reindexed codetags.");
    return 0;
}

/* ===== System-wide watcher support ===== */

typedef struct RepoCtx {
    char root[PATH_MAX];
    struct ignore ig;
    struct idmap map;
    struct cache fc;
    fs_watch_context wctx;
    int initialized;
} RepoCtx;

static int repoctx_init(RepoCtx *r, const char *root) {
    memset(r, 0, sizeof *r);
    strncpy(r->root, root, sizeof(r->root)-1);
    char oldcwd[PATH_MAX];
    if (!getcwd(oldcwd, sizeof oldcwd)) oldcwd[0] = 0;
    if (chdir(root) != 0) return -1;
    if (ensure_repo_workspace() != 0) { if (oldcwd[0]) chdir(oldcwd); return -1; }
    ignore_load(&r->ig, ".ctagsignore");
    if (idmap_open(&r->map, MAP_PATH, LASTID_PATH) != 0) { if (oldcwd[0]) chdir(oldcwd); return -1; }
    cache_open(&r->fc, FILECACHE_PATH);
    if (fs_watch_init(&r->wctx, root, &r->ig) != 0) {
        idmap_close(&r->map); cache_close(&r->fc); ignore_free(&r->ig);
        if (oldcwd[0]) chdir(oldcwd); return -1;
    }
    fs_walk_files(root, &r->ig, onfile_parse_repo, NULL, &r->map, &r->fc);
    md_rebuild(MD_PATH, &r->map);
    if (oldcwd[0]) chdir(oldcwd);
    r->initialized = 1;
    return 0;
}

static void repoctx_close(RepoCtx *r) {
    if (!r->initialized) return;
    fs_watch_close(&r->wctx);
    idmap_close(&r->map);
    cache_close(&r->fc);
    ignore_free(&r->ig);
    r->initialized = 0;
}

static int repoctx_process_event(RepoCtx *r) {
    fs_event ev;
    int rcode = fs_watch_next(&r->wctx, &ev);
    if (rcode <= 0) return rcode;
    char oldcwd[PATH_MAX];
    if (!getcwd(oldcwd, sizeof oldcwd)) oldcwd[0] = 0;
    chdir(r->root);
    if (ev.type == FS_EVENT_CREATE_DIR) {
        fs_watch_add_dir_recursive(&r->wctx, ev.path, &r->ig);
    } else if (ev.type == FS_EVENT_WRITE || ev.type == FS_EVENT_CREATE_FILE || ev.type == FS_EVENT_MOVE) {
        parse_file_inplace(ev.path, &r->map, &r->fc);
        md_rebuild(MD_PATH, &r->map);
    }
    fs_event_free(&ev);
    if (oldcwd[0]) chdir(oldcwd);
    return 1;
}

static RepoCtx* load_registry(size_t *out_count) {
    char reg[PATH_MAX];
    if (ensure_global_dirs(reg) != 0) return NULL;
    FILE *f = fopen(reg, "r");
    if (!f) return NULL;
    RepoCtx *repos = NULL; size_t n=0, cap=0;
    char *line = NULL; size_t capln = 0;
    while (getline(&line, &capln, f) > 0) {
        size_t L = strlen(line);
        while (L > 0 && (line[L-1] == '\n' || line[L-1] == '\r')) line[--L] = 0;
        if (L == 0) continue;
        if (cap == n) { cap = cap ? cap*2 : 8; repos = realloc(repos, cap*sizeof *repos); }
        if (repoctx_init(&repos[n], line) == 0) {
            n++;
        }
    }
    free(line);
    fclose(f);
    *out_count = n;
    return repos;
}

/* Registry file watch helpers */
static int open_registry(char out_path[PATH_MAX]) { return ensure_global_dirs(out_path); }
static int watch_registry_fd(const char *reg_path) {
    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0) return -1;
    int wd = inotify_add_watch(fd, reg_path, IN_MODIFY|IN_CLOSE_WRITE|IN_MOVE_SELF|IN_DELETE_SELF|IN_CREATE);
    if (wd < 0) { close(fd); return -1; }
    return fd;
}
static int drain_registry_events(int fd, int *needs_readd) {
    if (needs_readd) *needs_readd = 0;
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t len = read(fd, buf, sizeof buf);
    if (len <= 0) return 0;
    size_t i = 0; int saw = 0;
    while (i < (size_t)len) {
        struct inotify_event *ie = (struct inotify_event *)(buf + i);
        i += sizeof(*ie) + ie->len;
        saw = 1;
        if (needs_readd && (ie->mask & (IN_DELETE_SELF|IN_MOVE_SELF))) {
            *needs_readd = 1;
        }
    }
    return saw;
}

static int cmd_watch(void) {
    size_t count = 0;
    RepoCtx *repos = load_registry(&count);
    char reg_path[PATH_MAX];
    if (open_registry(reg_path) != 0) {
        fprintf(stderr, "Failed to open registry.\n");
        reg_path[0] = 0;
    }
    int reg_fd = -1;
    if (reg_path[0]) {
        reg_fd = watch_registry_fd(reg_path);
        if (reg_fd < 0) {
            fprintf(stderr, "Warning: failed to watch registry; falling back to periodic.\n");
        }
    }
    puts("codetags system watcher running.");
    unsigned long tick = 0;
    for (;;) {
        int progressed = 0;
        for (size_t i=0; i<count; i++) {
            int r = repoctx_process_event(&repos[i]);
            if (r > 0) progressed = 1;
        }
        if (reg_fd >= 0) {
            int needs_readd = 0;
            if (drain_registry_events(reg_fd, &needs_readd)) {
                size_t nc = 0;
                RepoCtx *nr = load_registry(&nc);
                if (nr) {
                    for (size_t i=0; i<count; i++) repoctx_close(&repos[i]);
                    free(repos);
                    repos = nr;
                    count = nc;
                    progressed = 1;
                }
            }
            if (needs_readd) {
                close(reg_fd);
                reg_fd = watch_registry_fd(reg_path);
            }
        } else {
            tick++;
            if (tick % 50 == 0) {
                size_t nc = 0;
                RepoCtx *nr = load_registry(&nc);
                if (nr) {
                    for (size_t i=0; i<count; i++) repoctx_close(&repos[i]);
                    free(repos);
                    repos = nr;
                    count = nc;
                    progressed = 1;
                }
            }
        }
        if (!progressed) usleep(100000);
    }
    if (reg_fd >= 0) close(reg_fd);
    for (size_t i=0; i<count; i++) repoctx_close(&repos[i]);
    free(repos);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 1; }
    const char *cmd = argv[1];
    if (strcmp(cmd, "init") == 0) {
        return cmd_init();
    } else if (strcmp(cmd, "scan") == 0) {
        if (argc < 3) { fprintf(stderr, "scan requires a path\n"); return 1; }
        return cmd_scan(argv[2]);
    } else if (strcmp(cmd, "watch") == 0) {
        return cmd_watch();
    } else if (strcmp(cmd, "groot") == 0) {
        return cmd_watch();
    } else if (strcmp(cmd, "reindex") == 0) {
        return cmd_reindex();
    } else {
        usage();
        return 1;
    }
}

