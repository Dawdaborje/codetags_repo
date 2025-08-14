#ifndef IGNORE_H
#define IGNORE_H
#include <stdbool.h>

struct ignore_rule {
    char *pattern;
    bool neg;
    bool only_dir;
    bool anchored;
    struct ignore_rule *next;
};

struct ignore {
    struct ignore_rule *rules;
};

int ignore_load(struct ignore *ig, const char *file);
bool ignore_match(const struct ignore *ig, const char *relpath, bool is_dir);
void ignore_free(struct ignore *ig);

#endif

