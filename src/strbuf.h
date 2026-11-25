#pragma once       

#include <stdlib.h>

typedef struct StrBuf {
    char  *data;   // heap block holding the text
    size_t len;    // how many characters are used
    size_t cap;    // how many bytes are allocated
} StrBuf;

void sb_init(StrBuf *sb); // setup an empty buffer

void sb_append(StrBuf *sb, const char *s, size_t n); // append n bytes from s

void sb_append_str(StrBuf *sb, const char *s); // append a c string(ends with \0)

void sb_append_char(StrBuf *sb, char c); // append one char

void sb_clear(StrBuf *sb); // clear the buffer but keep the memory

void sb_free(StrBuf *sb); // free the string buffer memory
