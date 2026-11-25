#include <string.h>
#include "strbuf.h"
#include "util.h"

static void sb_reserve(StrBuf *sb, size_t extra);

void sb_init(StrBuf *sb){
    sb->data = xmalloc(16); // reserves 16 bytes on start
    sb->len = 0;
    sb->cap = 16;
    sb->data[0] = '\0';
}

static void sb_reserve(StrBuf *sb, size_t extra){
    size_t needed = sb->len+extra+1; // we need +1 size for \0
    if (needed<=sb->cap)
        return;
    
    size_t new_cap = sb->cap ? sb->cap : 16; // preventing assigning 0 at first
    while (new_cap<needed){
        new_cap=new_cap*2; // we double the cap until our cap is enough
    }
    
    sb->data = xrealloc(sb->data,new_cap);
    sb->cap=new_cap;
}

void sb_append(StrBuf *sb, const char *s, size_t n){ // appends a char sequence until given length
    sb_reserve(sb,n); // first it reserves space if needed

    memcpy(sb->data + sb->len,s,n); // pointer aritmetic for destination where current string ends
    sb->len += n;
    sb->data[sb->len]='\0'; // add \0 to end
}

void sb_append_str(StrBuf *sb, const char *s){ // we append string directly without giving its length
    sb_append(sb,s,strlen(s));
}

void sb_append_char(StrBuf *sb, char c){ // appends a single char
    sb_append(sb,&c,1);
}

void sb_clear(StrBuf *sb){ // clears the content inside sb without freeing it
    sb->len=0;
    sb->data[0]='\0'; // we did not erase the other bytes as they will be overwritten and we allocated the memory already
}

void sb_free(StrBuf *sb){ // free's the string memory
    free(sb->data);
    // by setting these below prevents double freeing when an accidental sb_free call happens to same string
    sb->data=NULL;
    sb->len=0;
    sb->cap=0;
}



