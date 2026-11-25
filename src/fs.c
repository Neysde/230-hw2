#include <stdlib.h>
#include <string.h>
#include "fs.h"
#include "util.h"

// inodes (file content)

Inode *inode_new(void)
{
    Inode *inode = xmalloc(sizeof(Inode));
    inode->content = NULL; // an empty file has no content block yet
    inode->size = 0;
    inode->ref_count = 0;  // no name points to it yet; node_new_file adds the first one
    return inode;
}

void inode_release(Inode *inode)
{
    // hard links share the inode: free it only when the last name is gone
    inode->ref_count--;
    if (inode->ref_count == 0) {
        free(inode->content); // free(NULL) gives no error
        free(inode);
    }
}

void inode_write(Inode *inode, const char *data, size_t len, int append)
{
    if (!append) { // when >, first remove the old content
        free(inode->content);
        inode->content = NULL;
        inode->size = 0;
    }
    if (len == 0) // nothing to add
        return;

    // grow the block to hold the old bytes plus the new ones, then copy the new ones
    inode->content = xrealloc(inode->content, inode->size + len);
    memcpy(inode->content + inode->size, data, len); // content's pointer + size = address after the old bytes
    inode->size += len; // update the size
}

// creating node and directories

Directory *dir_create(Directory *parent, Node *self)
{
    Directory *d = xmalloc(sizeof(Directory)); // allocate directory's memory in heap
    d->self = self; // assign its node (root has no node)
    d->parent = parent; 
    d->entries = NULL; // the array is allocated on the first dir_add
    d->entry_count = 0;
    d->capacity = 0;
    return d;
}

// shared part of every node_new_* . allocate the node and fill the fields
static Node *node_alloc(const char *name, NodeType type, Directory *parent)
{
    Node *n = xmalloc(sizeof(Node));
    n->name = my_strdup(name); // own copy: the caller's string may be freed later
    n->type = type;
    n->parent = parent;
    return n;
}

Node *node_new_dir(const char *name, Directory *parent)
{
    Node *n = node_alloc(name, NODE_DIR, parent); // allocates directory node into heap
    n->data.dir_ref = dir_create(parent, n); // the directory points back to its node
    return n;
}

Node *node_new_file(const char *name, Directory *parent, Inode *inode)
{
    Node *n = node_alloc(name, NODE_FILE, parent);
    n->data.inode = inode;
    inode->ref_count++; // one more name now refers to this content
    return n;
}

Node *node_new_symlink(const char *name, Directory *parent, const char *target)
{
    Node *n = node_alloc(name, NODE_SYMLINK, parent);
    n->data.target_path = my_strdup(target); // the link stores the path text not a pointer to inode
    return n;
}

// entries array operations in directory

Node *dir_find(Directory *d, const char *name)
{
    for (size_t i = 0; i < d->entry_count; i++) {
        if (strcmp(d->entries[i]->name, name) == 0)
            return d->entries[i];
    }
    return NULL;
}

void dir_add(Directory *d, Node *n)
{
    // growing if full
    if (d->entry_count == d->capacity) {
        size_t new_cap = d->capacity ? d->capacity * 2 : 8; // preventing 0*2=0
        d->entries = xrealloc(d->entries, new_cap * sizeof(Node *));
        d->capacity = new_cap;
    }
    d->entries[d->entry_count] = n; // puts given node pointer to entries array
    d->entry_count++;
}

Node *dir_new_file(Directory *parent, const char *name)
{
    Node *n = node_new_file(name, parent, inode_new()); // ref_count 0 -> 1
    dir_add(parent, n);
    return n;
}

void dir_remove(Directory *d, Node *n)
{
    for (size_t i = 0; i < d->entry_count; i++) {
        if (d->entries[i] == n) { // compares pointers
            // shift the other entries one place left to remove the entry
            for (size_t j = i; j + 1 < d->entry_count; j++)
                d->entries[j] = d->entries[j + 1];
            d->entry_count--;
            return;
        }
    }
}

// free functions

// frees a directory, all of its entries and its entries array
static void dir_free(Directory *d)
{
    for (size_t i = 0; i < d->entry_count; i++)
        node_free(d->entries[i]);
    free(d->entries);
    free(d);
}

void node_free(Node *n)
{
    // the union in node holds a different thing for each type so check the type first
    switch (n->type) {
    case NODE_DIR:
        dir_free(n->data.dir_ref);
        break;
    case NODE_FILE:
        inode_release(n->data.inode);
        break;
    case NODE_SYMLINK:
        free(n->data.target_path);
        break;
    }
    free(n->name);
    free(n);
}

// path of a directory and shell setup

// appends every directory's name from the root down to d and put / in between
static void dir_path_rec(Directory *d, StrBuf *out)
{
    if (d->parent == NULL) // reached the root
        return;
    dir_path_rec(d->parent, out);
    sb_append_char(out, '/');
    sb_append_str(out, d->self->name);
}

void dir_path(Directory *d, StrBuf *out)
{
    if (d->parent == NULL) { // the root itself is just "/"
        sb_append_char(out, '/');
        return;
    }
    dir_path_rec(d, out);
}

void shell_init(Shell *sh)
{
    sh->root = dir_create(NULL, NULL); // the root has no parent and no node
    sh->cwd = sh->root;
}

void shell_destroy(Shell *sh)
{
    dir_free(sh->root); // frees the root, all of its entries and its entries array
    sh->root = NULL;
    sh->cwd = NULL;
}
