#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "strbuf.h"
#include "parser.h"

// private types
typedef struct { char *text; int is_op; } Token;               // is_op represents whether text is an unquoted > or >>
typedef struct { Token *items; size_t count; size_t cap; } TokenList; // count is how many tokens stored. cap is array's length

static void t1_push(TokenList *t1,char *text, int is_op){ // pushes an item into tokenlist
    
    // make size bigger if full
    if (t1->count==t1->cap){
        size_t new_cap = t1->cap ? t1->cap * 2 : 8; // preventing 0*2=0
        new_cap=new_cap*2;
        t1->items = xrealloc(t1->items, new_cap*sizeof(Token));
        t1->cap=new_cap;
    }

        t1->items[t1->count].text = text;
        t1->items[t1->count].is_op = is_op;
        t1->count++;
}

static void t1_free(TokenList *t1){ // frees the tokenlist
    for (size_t i = 0; i < t1->count; i++){
        free(t1->items[i].text); // free the strings pointed in the array one by one
    }
    free(t1->items); // free the array thus freeing is_op
    t1->items=NULL;
    t1->count=0;
    t1->cap=0;
}

static void finish_token(TokenList *t1, StrBuf *cur_str_buf,int was_quoted){
    int is_op = !was_quoted && (strcmp(cur_str_buf->data,">")==0 || strcmp(cur_str_buf->data,">>")==0); // determines whether there is an operator

    t1_push(t1,my_strdup(cur_str_buf->data),is_op); // pushes the created string into tokenlist
    sb_clear(cur_str_buf); // clears the content inside strbuf
}

// fills tokenlist with arguments with given line
static int tokenize(const char *line, TokenList *t1){
    StrBuf cur_str_buf;
    int in_token=0;
    int in_quotes=0;
    int was_quoted=0; // to determine whether a token is operation or just text

    sb_init(&cur_str_buf);

    for (size_t i = 0; line[i] != '\0'; i++){ // check all character in line

        if (line[i]=='\\'){ // cannot do \\0
            if (line[i+1]=='\0')
            {
                sb_free(&cur_str_buf);
                t1_free(t1);
                return -1;
            }
            sb_append_char(&cur_str_buf,line[i+1]); // append the char after \ directly
            in_token=1; // we go inside a token
            was_quoted=1; // we also act similar for \ (escape) to " (quote)
            i++;
            continue;
        } else if (in_quotes)
        {
            if (line[i]=='"')
            {
                in_quotes=0;
            } else
            {
                sb_append_char(&cur_str_buf,line[i]); // we append spaces too in quotes
            }
        } else if (line[i]=='"') // when we are not in quotes we go in quotes and we are inside a token
        {
            in_quotes=1;
            in_token=1;
            was_quoted=1;
        } else if (line[i]==' ' || line[i]=='\t'){ // space or tab
            if (in_token){ // if inside the token, end the token
                finish_token(t1,&cur_str_buf,was_quoted);
                in_token=0;
                was_quoted=0;
            }
        } else { // we append the char directly if above are not
            sb_append_char(&cur_str_buf,line[i]);
            in_token=1;
        }
    }

    if (in_quotes) // when line finishes and we still in quotes give error
    {
        sb_free(&cur_str_buf);
        t1_free(t1);
        return -1;
    }
    

    if (in_token){ // add the last token in line before finishing
        finish_token(t1,&cur_str_buf,was_quoted);
    }
    sb_free(&cur_str_buf); // free string buffer before exiting
    return 0;
}

// constructs the command struct from tokenlist
static int build_command(TokenList *t1, Command *cmd){

    int op_count=0;
    size_t op_index=0;

    for (size_t i = 0; i < t1->count; i++) // go through all tokens in t1
    {
        if (t1->items[i].is_op) // if that particular item is an operator
        {
            op_count++;
            op_index=i;
        }
    }
    if (op_count>0)
    {
        if (op_count>1) // can only have 1 operator
        {
            return -1;
        } else if (op_index==0) // operator cannot be at beginning
        {
            return -1;
        } else if (t1->count-op_index-1!=1) // operator must be exactly 1 before the last index
        {
            return -1;
        }
    }
    
    cmd->argc=(int)(op_count>0 ? op_index : t1->count); // if we have operator our argument count is before the operator, as operator and destination are not counted

    cmd->argv = xmalloc((cmd->argc+1)*sizeof(char *)); // we allocate command argument array memory

    for(int i = 0;i<cmd->argc;i++){  // go through all argument indexes in command and write the corresponding argument text inside
        cmd->argv[i]=t1->items[i].text;
    }
    cmd->argv[cmd->argc]=NULL;  // this is like \0 in string add to the end

    // add operation type if we have operator
    if (op_count>0) 
    {
        if (strcmp(t1->items[op_index].text, ">") == 0){
            cmd->redir=REDIR_OUT;
        } else {
            cmd->redir=REDIR_APPEND;
        }
        
        cmd->redir_target=t1->items[op_index+1].text; // get redirection target and write to cmd
        free(t1->items[op_index].text);
    } else { // when there is no operator
        cmd->redir = REDIR_NONE;
        cmd->redir_target = NULL;
    }

    // free tokenlist before exiting
    free(t1->items);
    t1->items=NULL;
    t1->count=0;
    t1->cap=0;

    return 0;
}

// parse the written line inside shell by first tokenizing and with created tokenlist build command struct
int parse_line(const char *line, Command *cmd){
    TokenList t1 = {NULL,0,0};

    if (tokenize(line,&t1)) // if tokenize returns -1, there is an error and we quit from the function
    {
        return -1;
    }

    if (build_command(&t1,cmd)) // if build command has got an error
    {
        t1_free(&t1); // we need to free the tokenlist as we already filled it
        return -1;
    }
    return 0;
    
}

// free command struct
void command_free(Command *cmd){
    for(int i=0;i<cmd->argc;i++){
        free(cmd->argv[i]);
    }
    free(cmd->argv);
    cmd->argc=0;
    cmd->redir=REDIR_NONE;
    free(cmd->redir_target);
    cmd->redir_target=NULL;
}

