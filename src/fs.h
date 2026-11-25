#pragma once

#include <stdlib.h>
#include "strbuf.h"

typedef enum {
	NODE_FILE,
	NODE_DIR,
	NODE_SYMLINK
} NodeType;

// stores file's data
typedef struct Inode {
	char *content; // pointer to a heap block with file's bytes (this is not a string)
	size_t size; // how many bytes in that block
	size_t ref_count; // How many file names (nodes) currently pointing
} Inode;


typedef struct Directory Directory;

typedef struct Node {
	char *name; // name of the file/node/symlink
	NodeType type; // whether its file, dir or symlink
	Directory *parent; // node's directory that this node is in.
	union {
		Inode *inode; // file uses that
		char *target_path; // symlink uses that
		Directory *dir_ref; // directory uses that
	} data;
} Node;

struct Directory {
    Node *self;          // the Node that holds this directory (NULL for root)
	Directory *parent;   // the folder above this folder (NULL for root)
	Node **entries;		 // holds directory contents (array holding node pointers)
	size_t entry_count;  // how many entries are used
	size_t capacity;     // how many entries are allocated
};

typedef struct Shell {
    Directory *root; // points to the top folder (/)
    Directory *cwd; // points to the current working directory
} Shell;

// creates an empty file content block (ref_count) (node_new_file increments it)
Inode *inode_new(void);

// removes one reference to inode frees the content when ref_count=0
void inode_release(Inode *inode);

// writes len bytes from data into inode append = 0 replaces the old content append = 1 appends.
void inode_write(Inode *inode, const char *data, size_t len, int append);

// creates an empty directory self is the node holding it (self=NULL for root as only directory has no node is root)
Directory *dir_create(Directory *parent, Node *self);

// creates a directory node inside parent (it does not add it to parent's entries)
Node *node_new_dir(const char *name, Directory *parent);

// creates a file node that points to inode, and increments inode's ref_count
// (it does not add the node to parent's entries, it just creates the node) used for new files and hard links
Node *node_new_file(const char *name, Directory *parent, Inode *inode);

// creates a symlink node that stores a copy of target
// (it does not add the node to parent's entries, it just creates the node)
Node *node_new_symlink(const char *name, Directory *parent, const char *target);

// returns the entry, name written in name field which is in d. or NULL if there is none
Node *dir_find(Directory *d, const char *name);

// appends n to d's entries, grows the array by doubling
void dir_add(Directory *d, Node *n);

// It adds pointer of the node created by node_new_file to its parent dir's entries array and returns the pointer of created node
Node *dir_new_file(Directory *parent, const char *name);

// takes n out of d's entries without freeing it (the caller of this fn frees or moves it)
void dir_remove(Directory *d, Node *n);

// frees n and everything it owns (recursively for directories)
void node_free(Node *n);

// appends the absolute path of d to out
void dir_path(Directory *d, StrBuf *out);

// creates the root directory and sets cwd to it
void shell_init(Shell *sh);

// frees the whole file system
void shell_destroy(Shell *sh);

