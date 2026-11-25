#pragma once

#include <stdlib.h>

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *my_strdup(const char *s);

