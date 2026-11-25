#pragma once

#include <stdlib.h>

typedef enum { REDIR_NONE, REDIR_OUT, REDIR_APPEND } RedirType;   // none, >, >>

typedef struct Command {
    int argc;             // argument count
    char **argv;          // (similar to *argv[]) string number inside is argc and a NULL at the end
    RedirType redir;        // redirection type
    char *redir_target;   // NULL when redir == REDIR_NONE
} Command;

int  parse_line(const char *line, Command *cmd);  // 0 = ok, -1 = syntax error
void command_free(Command *cmd); // frees the command
