#pragma once

#include "fs.h"      // for shell
#include "strbuf.h" 

/* Every command has
sh=shell state (for root and cwd)
argc=number of arguments
argv=the arguments itself. argv[0] is the command name argv[argc] is NULL
out=output is appended here commands never use print directly
Returns 0 on success, -1 on error (with -1, main prints error message) */

typedef int (*CmdFn)(Shell *sh, int argc, char **argv, StrBuf *out); // is a pointer to a function

int cmd_echo(Shell *sh, int argc, char **argv, StrBuf *out);
int cmd_ls(Shell *sh, int argc, char **argv, StrBuf *out);
int cmd_tree(Shell *sh, int argc, char **argv, StrBuf *out);
int cmd_cat(Shell *sh, int argc, char **argv, StrBuf *out);
int cmd_du(Shell *sh, int argc, char **argv, StrBuf *out);
int cmd_cp(Shell *sh, int argc, char **argv, StrBuf *out);

int cmd_pwd(Shell *sh, int argc, char **argv, StrBuf *out);
int cmd_cd(Shell *sh, int argc, char **argv, StrBuf *out);
int cmd_mkdir(Shell *sh, int argc, char **argv, StrBuf *out);
int cmd_touch(Shell *sh, int argc, char **argv, StrBuf *out);
int cmd_rm(Shell *sh, int argc, char **argv, StrBuf *out);
int cmd_ln(Shell *sh, int argc, char **argv, StrBuf *out);   //handles both ln and ln -s
int cmd_mv(Shell *sh, int argc, char **argv, StrBuf *out);
