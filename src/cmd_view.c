#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "commands.h"
#include "path.h"

// helpers shared by the commands below

// Returns the inode of the regular file at path, or NULL if path is missing,
// is a directory, or is a broken or looping symlink (symlinks are followed).
static Inode *find_file(Shell *sh, const char *path)
{
    PathRef ref;
    if (lookup(sh, path, 1, &ref) != 0)
        return NULL;

    Node *n = ref.node;
    path_ref_free(&ref); // we only need the node, not the name
    if (n == NULL || n->type != NODE_FILE)
        return NULL;
    return n->data.inode;
}

// qsort comparator for an array of Node pointers: sorts by name (strcmp order).
static int cmp_node_name(const void *a, const void *b)
{
    const Node *na = *(Node *const *)a;
    const Node *nb = *(Node *const *)b;
    return strcmp(na->name, nb->name);
}

// sorts a directory's entries alphabetically
static void sort_entries(Directory *d)
{
    if (d->entry_count > 1)
        qsort(d->entries, d->entry_count, sizeof(Node *), cmp_node_name);
}

// appends an entry's name as ls and tree show it: "dir/", "link@", "file"
static void append_entry_name(StrBuf *out, const Node *n)
{
    sb_append_str(out, n->name);
    if (n->type == NODE_DIR)
        sb_append_char(out, '/');
    else if (n->type == NODE_SYMLINK)
        sb_append_char(out, '@');
}

// commands

// echo [text...]: prints the arguments separated by single spaces, then a newline
int cmd_echo(Shell *sh, int argc, char **argv, StrBuf *out)
{
    (void)sh;
    for (int i = 1; i < argc; i++)
    {
        if (i>1)
        {
            sb_append_str(out," "); // add space between arguments
        }
        sb_append_str(out,argv[i]);
    }
    sb_append_str(out,"\n");

    return 0;

}

// ls [-a] [path]: lists a directory's entries sorted by name, separated by one space. names starting with '.' are hidden unless -a is given; -a also lists "./" and "../".
int cmd_ls(Shell *sh, int argc, char **argv, StrBuf *out)
{
    int show_all = 0;
    const char *path = NULL;

    // options and the path may come in any order: "ls -a docs" or "ls docs -a"
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            if (strcmp(argv[i], "-a") != 0) // the only supported option
                return -1;
            show_all = 1;
        } else {
            if (path != NULL) // only one path is supported
                return -1;
            path = argv[i];
        }
    }

    Directory *d = (path == NULL) ? sh->cwd : walk_dir(sh, sh->cwd, path);
    if (d == NULL) // missing, or not a directory
        return -1;

    sort_entries(d);

    int printed = 0; // whether a name was written yet, to know if a separator is needed
    if (show_all) {
        sb_append_str(out, "./ ../"); // one space like every other name
        printed = 1;
    }
    for (size_t i = 0; i < d->entry_count; i++) {
        Node *n = d->entries[i];
        if (!show_all && n->name[0] == '.') // hidden entry
            continue;
        if (printed)
            sb_append_char(out, ' ');
        append_entry_name(out, n);
        printed = 1;
    }
    sb_append_char(out, '\n');
    return 0;
}

// prints every entry of d, sorted, indented by 2 spaces per depth level recurses into subdirectories (symlinks are shown with '@' and not entered)
static void tree_print(Directory *d, int depth, StrBuf *out)
{
    sort_entries(d);
    for (size_t i = 0; i < d->entry_count; i++) {
        Node *n = d->entries[i];
        for (int s = 0; s < depth * 2; s++)
            sb_append_char(out, ' ');
        append_entry_name(out, n);
        sb_append_char(out, '\n');
        if (n->type == NODE_DIR)
            tree_print(n->data.dir_ref, depth + 1, out);
    }
}

// tree [path] shows the directory and everything below it. the first line is "/" for the root otherwise the directory's name with '/'.
int cmd_tree(Shell *sh, int argc, char **argv, StrBuf *out)
{
    if (argc > 2)
        return -1;

    Directory *d = (argc == 2) ? walk_dir(sh, sh->cwd, argv[1]) : sh->cwd;
    if (d == NULL) // missing, or not a directory
        return -1;

    if (d->parent == NULL) { // the root has no Node, so no name
        sb_append_char(out, '/');
    } else {
        sb_append_str(out, d->self->name);
        sb_append_char(out, '/');
    }
    sb_append_char(out, '\n');

    tree_print(d, 1, out);
    return 0;
}

// cat <file> prints the file's content exactly as stored
int cmd_cat(Shell *sh, int argc, char **argv, StrBuf *out)
{
    if (argc != 2)
        return -1;

    Inode *inode = find_file(sh, argv[1]);
    if (inode == NULL) // missing, or not a regular file
        return -1;

    if (inode->size > 0) // an empty file has content == NULL nothing to print
        sb_append(out, inode->content, inode->size);
    return 0;
}

// du <file> prints "<size>\t<file>" with the size in bytes and the path as typed
int cmd_du(Shell *sh, int argc, char **argv, StrBuf *out)
{
    if (argc != 2)
        return -1;

    Inode *inode = find_file(sh, argv[1]);
    if (inode == NULL) // missing or not a regular file
        return -1;

    char size_text[32]; // 32 byte big enough for any size_t written in decimal
    snprintf(size_text, sizeof size_text, "%zu", inode->size);

    sb_append_str(out, size_text);
    sb_append_char(out, '\t');
    sb_append_str(out, argv[1]);
    sb_append_char(out, '\n');
    return 0;
}

// cp <src> <dest> deep copy of a regular file (a new inode with its own copy of the content). if dest is an existing directory, the copy goes inside it with src's name; if dest is an existing file, its content is replaced.
int cmd_cp(Shell *sh, int argc, char **argv, StrBuf *out)
{
    (void)out; // silent on success
    if (argc != 3)
        return -1;

    // the source must be an existing file
    PathRef src;
    if (lookup(sh, argv[1], 1, &src) != 0) // find the source file
        return -1;
    if (src.node == NULL || src.node->type != NODE_FILE) { // missing or a directory
        path_ref_free(&src);
        return -1;
    }
    Inode *src_inode = src.node->data.inode;

    // where the copy goes is "cp a.txt docs" -> docs/a.txt, "cp a.txt b.txt" -> b.txt
    // (follow_last = 1: copying onto a symlink writes into the file it points to)
    PathRef dest;
    if (lookup_dest(sh, argv[2], src.name, 1, &dest) != 0) { // decide where the copy goes (!! lookup_dest finds whether destination is folder or not and it fills dest pathref according to that) (if destination's directory missing)
        path_ref_free(&src);
        return -1;
    }
    Node *existing = dest.node;

    int rc = 0; // return code (we use this to do cleaning only at the end)
    if (existing == NULL && dest.dir_only) {
        rc = -1; // "cp a.txt new/": a trailing '/' can't name the new file
    } else if (existing == NULL) { // if we do not have node where copy goes
        // a new inode, so later changes to one file don't affect the other
        Node *copy = dir_new_file(dest.parent, dest.name);
        inode_write(copy->data.inode, src_inode->content, src_inode->size, 0);
    } else if (existing->type != NODE_FILE) { // a directory with that name is in the way
        rc = -1; 
    } else if (existing->data.inode == src_inode) {
        rc = -1; // copying a file onto itself (or onto its own hard link)
    } else {
        // existing file replace its content (its hard links see the new content too)
        inode_write(existing->data.inode, src_inode->content, src_inode->size, 0);
    }

    path_ref_free(&dest);
    path_ref_free(&src);
    return rc;
}
