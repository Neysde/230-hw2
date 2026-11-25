#include <stdlib.h>
#include <string.h>
#include "commands.h"
#include "path.h"
#include "util.h"

// pwd prints the absolute path of the current directory
int cmd_pwd(Shell *sh, int argc, char **argv, StrBuf *out)
{
    (void)argv; // we do not use that argument in fn for pwd
    if (argc != 1) // pwd takes no arguments
        return -1;
    dir_path(sh->cwd, out);
    sb_append_char(out, '\n');
    return 0;
}

// cd <path> changes the current directory (absolute or relative with . and ..)
int cmd_cd(Shell *sh, int argc, char **argv, StrBuf *out)
{
    (void)out; // cd prints nothing on success
    if (argc != 2)
        return -1;

    Directory *d = walk_dir(sh, sh->cwd, argv[1]);
    if (d == NULL) // missing, or some component is not a directory
        return -1;
    sh->cwd = d;
    return 0;
}

// mkdir <path> creates an empty directory. its parent must exist
int cmd_mkdir(Shell *sh, int argc, char **argv, StrBuf *out)
{
    (void)out; // prints nothing on success
    if (argc != 2)
        return -1;

    PathRef ref;
    if (lookup(sh, argv[1], 0, &ref) != 0) // parent missing or "/", ".", ".."
        return -1;

    int rc = 0; // return code (we use that to use free once before the return at the end)
    if (ref.node != NULL) { // a file, directory or link with that name exists
        rc = -1;
    } else {
        dir_add(ref.parent, node_new_dir(ref.name, ref.parent));
    }

    path_ref_free(&ref);
    return rc;
}

// touch <path> creates an empty file if nothing with that name exists
int cmd_touch(Shell *sh, int argc, char **argv, StrBuf *out)
{
    (void)out; // prints nothing on success
    if (argc != 2)
        return -1;

    PathRef ref;
    // follow_last = 0: an existing symlink counts as existing it is not followed
    if (lookup(sh, argv[1], 0, &ref) != 0) { // parent missing or "/", ".", ".."
        // "/", ".", ".." (and "x/.") name an existing directory: nothing to do
        return (walk_dir(sh, sh->cwd, argv[1]) != NULL) ? 0 : -1;
    }

    int rc = 0; // return code
    // if the entry already exists touch has nothing to do (there are no timestamps)
    if (ref.node == NULL) {
        if (ref.dir_only) // "touch new/": a trailing '/' can't name a new file
            rc = -1;
        else
            dir_new_file(ref.parent, ref.name);
    }

    path_ref_free(&ref);
    return rc;
}

// returns 1 if d is ancestor itself or lies somewhere below it 0 otherwise
static int dir_is_inside(Directory *d, Directory *ancestor)
{
    for (; d != NULL; d = d->parent) { // walk up towards the root
        if (d == ancestor)
            return 1;
    }
    return 0;
}

// rm <path> removes a file, a symlink (only the link) or an empty directory. for a file, only this name goes away; the content is freed when no hard link is left.
int cmd_rm(Shell *sh, int argc, char **argv, StrBuf *out)
{
    (void)out; // prints nothing on success
    if (argc != 2)
        return -1;

    PathRef ref;
    // follow_last = 0: "rm link" removes the link, not the file it points to.
    // lookup also refuses "/", "." and "..", so the root can never be removed.
    if (lookup(sh, argv[1], 0, &ref) != 0)
        return -1;

    Node *n = ref.node;
    int rc = 0; // return code
    if (n == NULL) {
        rc = -1; // nothing with that name
    } else if (n->type == NODE_DIR && (n->data.dir_ref->entry_count > 0 || dir_is_inside(sh->cwd, n->data.dir_ref))) {
        // a non-empty directory can't be removed neither can the current directory (or one of its parents), because sh->cwd would point to freed memory
        rc = -1;
    } else {
        dir_remove(ref.parent, n); // take it out of its directory first
        node_free(n);              // then free it (for files, inode_release decides)
    }

    path_ref_free(&ref);
    return rc;
}

// ln <src> <dst> a hard link, a second name pointing to the same inode
static int make_hard_link(Shell *sh, const char *src_path, const char *dst_path)
{
    PathRef src;
    if (lookup(sh, src_path, 0, &src) != 0)
        return -1;
    // hardlink allows only linking to regular files, linking a directory or a symlink is not allowed
    if (src.node == NULL || src.node->type != NODE_FILE) {
        path_ref_free(&src);
        return -1;
    }

    PathRef dst;
    if (lookup_dest(sh, dst_path, src.name, 0, &dst) != 0) { // destination's dir missing
        path_ref_free(&src);
        return -1;
    }

    int rc = 0; // return code
    if (dst.node != NULL) {
        rc = -1; // the new name is already taken so we cannot overwrite it
    } else if (dst.dir_only) {
        rc = -1; // "ln a.txt new/" a trailing '/' can't name the new link
    } else {
        // the new node shares src's inode; node_new_file increments its ref_count
        dir_add(dst.parent, node_new_file(dst.name, dst.parent, src.node->data.inode));
    }

    path_ref_free(&dst);
    path_ref_free(&src);
    return rc;
}

// ln -s <target> <link> a symbolic link that stores target(the path of an existing file that link will point to). the target must be an existing regular file when the link is created. link_path is the where the new symlink is created
static int make_symlink(Shell *sh, const char *target, const char *link_path)
{
    PathRef link;
    if (lookup(sh, link_path, 0, &link) != 0)
        return -1;
    if (link.node != NULL) { // the link's name is already taken
        path_ref_free(&link);
        return -1;
    }
    if (link.dir_only) { // "ln -s x new/": a trailing '/' can't name the new link
        path_ref_free(&link);
        return -1;
    }

    // a relative target is relative to the directory the link is in
    PathRef t;
    int rc = -1; // return code
    if (lookup_at(sh, link.parent, target, 0, &t) == 0) { // check the target from the link's folder
        // links to directories or to other links are not allowed
        if (t.node != NULL && t.node->type == NODE_FILE) {
            dir_add(link.parent, node_new_symlink(link.name, link.parent, target));
            rc = 0;
        }
        path_ref_free(&t);
    }

    path_ref_free(&link);
    return rc;
}

// ln <src> <dst> or ln -s <target> <link>
int cmd_ln(Shell *sh, int argc, char **argv, StrBuf *out)
{
    (void)out; // silent on success
    if (argc == 4 && strcmp(argv[1], "-s") == 0)
        return make_symlink(sh, argv[2], argv[3]);
    if (argc == 3 && strcmp(argv[1], "-s") != 0)
        return make_hard_link(sh, argv[1], argv[2]);
    return -1; // wrong number of arguments, e.g. "ln -s" or "ln a"
}

// mv <src> <dest> moves or renames an entry by moving its node pointer. if dest is an existing directory, src goes inside it; if dest is an existing file (and src is not a directory), that file is replaced.
int cmd_mv(Shell *sh, int argc, char **argv, StrBuf *out)
{
    (void)out; // silent on success
    if (argc != 3)
        return -1;

    PathRef src;
    // follow_last = 0: moving a symlink moves the link itself
    if (lookup(sh, argv[1], 0, &src) != 0)
        return -1;
    if (src.node == NULL) {
        path_ref_free(&src);
        return -1;
    }

    PathRef dst;
    if (lookup_dest(sh, argv[2], src.name, 0, &dst) != 0) { // destination's dir missing
        path_ref_free(&src);
        return -1;
    }

    Node *n = src.node;
    Node *existing = dst.node;
    int rc = 0; // return code

    if (existing == n) {
        // "mv a a" already there nothing to do
    } else if (n->type == NODE_DIR && dir_is_inside(dst.parent, n->data.dir_ref)) {
        // "mv a a/b" a directory can't be moved into itself or below itself;
        // it would be cut off from the tree (a leak and an endless loop)
        rc = -1;
    } else if (existing != NULL && (existing->type == NODE_DIR || n->type == NODE_DIR)) {
        // a directory can't replace anything and nothing replaces a directory
        rc = -1;
    } else if (existing == NULL && dst.dir_only && n->type != NODE_DIR) {
        // "mv a.txt new/" a trailing '/' can only rename a directory
        rc = -1;
    } else {
        if (existing != NULL) { // replacing a file or link remove the old one first
            dir_remove(dst.parent, existing);
            node_free(existing);
        }

        dir_remove(src.parent, n); // unhook from the old directory (not freed)

        free(n->name); // give it its new name
        n->name = my_strdup(dst.name);
        n->parent = dst.parent;
        if (n->type == NODE_DIR) // a directory also stores its parent itself
            n->data.dir_ref->parent = dst.parent;

        dir_add(dst.parent, n); // hook into the new directory
    }

    path_ref_free(&dst);
    path_ref_free(&src);
    return rc;
}
