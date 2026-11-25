#pragma once

#include "fs.h"


// Path resolution.

// more than that means a loop (a -> b -> a) or a chain that is too long
#define MAX_SYMLINK_HOPS 16

// What lookup found for a path like "a/b/name".
typedef struct PathRef {
    Directory *parent; // the directory that holds the last component
    char *name;        // the last component as a heap copy; freed by path_ref_free
    Node *node;        // the existing entry with that name, or NULL if there is none
    int dir_only;      // 1 if the path ended in '/' it may only name (or create) a directory
} PathRef;

// Resolves path to a directory, starting at start for relative paths. Returns NULL if a component is missing or is not a directory (or a symlink to one).
Directory *walk_dir(Shell *sh, Directory *start, const char *path);

// Resolves everything except the last component of path to a directory, then
// looks the last component up in it. Fills ref and returns 0 when that parent directory exists
// Returns -1, with ref left empty, when the parent does not exist or the path ends in ".", ".." or "/"
// follow_last = 1 if the entry is a symlink ref describes the file it points to. a broken link or a loop returns -1
// follow_last = 0 the symlink is returned as itself (for rm, mv, ln)
int lookup(Shell *sh, const char *path, int follow_last, PathRef *ref);

// Same as lookup but it we can choose any starting folder instead of cwd. we use this to make symlink
int lookup_at(Shell *sh, Directory *start, const char *path, int follow_last, PathRef *ref);

// Finds where (cp, mv, ln) puts its result. If dest is an existing folder the result goes inside it under src_name, otherwise it is destination itself.
int lookup_dest(Shell *sh, const char *dest, const char *src_name, int follow_last, PathRef *ref);

// frees what lookup allocated in ref (safe on an empty ref).
void path_ref_free(PathRef *ref);
