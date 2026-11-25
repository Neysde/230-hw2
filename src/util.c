#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"

void *xmalloc(size_t n){
    if (n == 0) n = 1; // zero size can be mistaken out of memory

    void *p = malloc(n);
    if (p==NULL){
        fprintf(stderr, "bushell: out of memory\n");

        exit(EXIT_FAILURE); // ends the whole program like System.exit(1);
    }
    return p;
}

void *xrealloc(void *p, size_t n){
    if (n == 0) n = 1; // zero size can be mistaken out of memory

    void *q = realloc(p,n);
    if (q==NULL){
        fprintf(stderr, "bushell: out of memory\n");

        exit(EXIT_FAILURE);
    }
    return q;
}

char *my_strdup(const char *s){ // copies string
    size_t len = strlen(s);

    char *p = xmalloc(len+1); // to include \0 too

    return memcpy(p,s,len+1); // void * converted to char * automatically
}
