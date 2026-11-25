#include <stdlib.h>
#include <string.h>
#include "path.h"
#include "util.h"

/*
 * The three internal functions below call each other (a symlink target is itself
 * a path), and all of them share one hop counter so that the limit of
 * MAX_SYMLINK_HOPS applies to the whole resolution of a path.
 */
static Directory *walk_from(Shell *sh, Directory *start, const char *path, int *hops);
static int lookup_from(Shell *sh, Directory *start, const char *path,
                       int follow_last, PathRef *ref, int *hops);
static Node *follow(Shell *sh, Node *n, int *hops);

// follows n while it is a symlink. Returns the final non-symlink nod  or NULL if a link is broken or more than MAX_SYMLINK_HOPS are needed.
static Node *follow(Shell *sh, Node *n, int *hops)
{
    while (n != NULL && n->type == NODE_SYMLINK) {
        (*hops)++;
        if (*hops > MAX_SYMLINK_HOPS) // a loop like a -> b -> a would never end so we use MAX_SYMLINK_HOPS
            return NULL;

        // a relative target in symlink is relative to the directory that contains the link so we lookup with its parent node as start (not cwd)
        PathRef target;
        if (lookup_from(sh, n->parent, n->data.target_path, 0, &target, hops) != 0)
            return NULL;
        n = target.node; // NULL if nothing has that name any more (broken link)
        path_ref_free(&target);
    }
    return n;
}

// If ref->node is a symlink, replaces ref with the entry the link points to. returns -1 (and empties ref) if the link is broken or loops.
static int follow_ref(Shell *sh, PathRef *ref, int *hops)
{
    if (ref->node == NULL || ref->node->type != NODE_SYMLINK)
        return 0; // nothing to follow

    Node *target = follow(sh, ref->node, hops);
    if (target == NULL) {
        path_ref_free(ref);
        return -1;
    }
    free(ref->name);
    ref->name = my_strdup(target->name);
    ref->parent = target->parent;
    ref->node = target;
    return 0;
}

// walks from cwd normally (but from link's folder for symlinks)
static Directory *
walk_from(Shell *sh, Directory *start, const char *path, int *hops)
{
    if (path[0] == '\0') // an empty path names nothing
        return NULL;

    Directory *d = (path[0] == '/') ? sh->root : start; // if path starts with / ,it starts from root. if not it starts from cwd (but for symlink its the link's folder not wherever you are currently)

    // Work on a copy so we can cut it into string components by writing \0 over each /
    char *copy = my_strdup(path);

    // strtok_r does the cutting it keeps its position in save variable instead of hidden global state so the nested call through follow() is safe
    // It also skips empty components so "a//b" and a trailing '/' need no special case.
    char *save;
    for (char *comp = strtok_r(copy, "/", &save); comp != NULL; comp = strtok_r(NULL, "/", &save)) {
        if (strcmp(comp, ".") == 0) {
            // . stay in the same directory
        } else if (strcmp(comp, "..") == 0) { // go to the parent dir
            if (d->parent != NULL) // the parent of the root is the root
                d = d->parent;
        } else {
            Node *n = dir_find(d, comp);
            if (n != NULL && n->type == NODE_SYMLINK) // a link in the middle of a path
                n = follow(sh, n, hops);
            if (n == NULL || n->type != NODE_DIR) { // missing, broken link, or a file
                free(copy);
                return NULL;
            }
            d = n->data.dir_ref;
        }
    }

    free(copy);
    return d;
}

// helper func for lookup (start is cwd)
static int lookup_from(Shell *sh, Directory *start, const char *path, int follow_last, PathRef *ref, int *hops)
{
    ref->parent = NULL;
    ref->name = NULL;
    ref->node = NULL;
    ref->dir_only = 0;

    if (path[0] == '\0')
        return -1;

    char *copy = my_strdup(path);

    // remove / from end of the path to prevent recognizing the file name as ""
    size_t len = strlen(copy);
    int had_slash = (len > 1 && copy[len - 1] == '/'); // a trailing '/' means a directory
    while (len > 1 && copy[len - 1] == '/') {
        copy[len - 1] = '\0';
        len--;
    }

    // Split at the last '/' everything before it is the parent path, everything after it is the name of the entry.
    const char *parent_path;
    const char *base;
    char *slash = strrchr(copy, '/'); // locates the last occurance of a char in a str and returns a pointer to it
    if (slash == NULL) {          // when "name", parent is start
        parent_path = NULL;
        base = copy;
    } else if (slash == copy) {   // when "/name", "/" . parent is the root
        parent_path = "/";
        base = slash + 1;
    } else {                      // when "a/b/name" . parent is "a/b"
        *slash = '\0';
        parent_path = copy; // saves the full path pointer but we put \0 so it actually gets a/b as string
        base = slash + 1; // and base is pointing after \0 we put so it takes name part as string
    }

    // "/", "x/." and "x/.." name a directory itself, not an entry inside a parent
    if (base[0] == '\0' || strcmp(base, ".") == 0 || strcmp(base, "..") == 0) {
        free(copy);
        return -1;
    }

    Directory *parent = (parent_path == NULL) ? start : walk_from(sh, start, parent_path, hops); // start is cwd
    if (parent == NULL) { // the directory part does not exist
        free(copy);
        return -1;
    }

    ref->parent = parent;
    ref->name = my_strdup(base); // base points into copy, which is freed below
    ref->node = dir_find(parent, base);
    free(copy);

    // With a trailing '/' the path must name a directory an existing entry that is not one (looking through a symlink) is an error, and a missing one may only be created as a directory, which the creating commands check with dir_only.
    if (had_slash) {
        ref->dir_only = 1;
        Node *n = ref->node;
        if (n != NULL && n->type == NODE_SYMLINK)
            n = follow(sh, n, hops);
        if (ref->node != NULL && (n == NULL || n->type != NODE_DIR)) {
            path_ref_free(ref);
            return -1;
        }
    }

    if (follow_last)
        return follow_ref(sh, ref, hops);
    return 0;
}

// public functions each starts a fresh hop counter(hops counts how many symlinks followed while resolving a path. If it goes over 16 it stops with an error)

Directory *walk_dir(Shell *sh, Directory *start, const char *path)
{
    int hops = 0;
    return walk_from(sh, start, path, &hops);
}

int lookup(Shell *sh, const char *path, int follow_last, PathRef *ref)
{
    int hops = 0;
    return lookup_from(sh, sh->cwd, path, follow_last, ref, &hops);
}

int lookup_at(Shell *sh, Directory *start, const char *path, int follow_last, PathRef *ref)
{
    int hops = 0;
    return lookup_from(sh, start, path, follow_last, ref, &hops);
}

int lookup_dest(Shell *sh, const char *dest, const char *src_name, int follow_last, PathRef *ref)
{
    Directory *d = walk_dir(sh, sh->cwd, dest);
    if (d == NULL) // if not an existing directory, dest names the result itself
        return lookup(sh, dest, follow_last, ref);

    // dest is a directory (ex: "cp a.txt docs"), the result is docs/src_name
    ref->parent = d;
    ref->name = my_strdup(src_name);
    ref->node = dir_find(d, src_name);
    ref->dir_only = 0; // the '/' in dest belongs to the folder not to the new entry's name
    if (follow_last) { // if this is 1, it gives the symlink's target
        int hops = 0;
        return follow_ref(sh, ref, &hops);
    }
    return 0;
}

void path_ref_free(PathRef *ref)
{
    free(ref->name); // free(NULL) is fine for an empty ref
    ref->parent = NULL;
    ref->name = NULL;
    ref->node = NULL;
    ref->dir_only = 0;
}
