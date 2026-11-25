#include <stdio.h>
#include <string.h>
#include "fs.h"
#include "path.h"
#include "commands.h"
#include "parser.h"

#define LINE_BUF_SIZE 1024 // input is at most 1024 bytes; room for '\n' and '\0'

typedef struct CmdEntry {const char *name; CmdFn fn;} CmdEntry;

// command names and their corresponding functions
static const CmdEntry cmd_table[] = {
    { "echo",  cmd_echo  },
    { "ls",    cmd_ls    },
    { "tree",  cmd_tree  },
    { "cat",   cmd_cat   },
    { "du",    cmd_du    },
    { "cp",    cmd_cp    },
    { "pwd",   cmd_pwd   },
    { "cd",    cmd_cd    },
    { "mkdir", cmd_mkdir },
    { "touch", cmd_touch },
    { "rm",    cmd_rm    },
    { "ln",    cmd_ln    },   
    { "mv",    cmd_mv    },
};

// searches through the cmd table with given command name to find the corresponding function
static CmdFn find_command(const char *name){
    for(size_t i=0;i<sizeof cmd_table / sizeof cmd_table[0];i++){
        if (strcmp(name,cmd_table[i].name)==0)
            return cmd_table[i].fn;
    }
    return NULL;
}

static void print_error(void) {
    printf("bushell: error\n");
}

// opens the target of cmd > target or cmd >> target before the command runs
// so the path is resolved from the current directory and a missing file is created
// returns the inode of the file with one extra reference or null if target cannot be a file
static Inode *open_redirect(Shell *sh, RedirType mode, const char *target)
{
    PathRef ref;
    if (lookup(sh, target, 1, &ref) != 0) // parent directory missing or "/", ".", ".."
        return NULL;

    Node *file = ref.node;
    if (file == NULL && ref.dir_only) { // "> new/" a trailing '/' can't name a new file
        path_ref_free(&ref);
        return NULL;
    }
    if (file == NULL) {
        file = dir_new_file(ref.parent, ref.name); // ">" or ">>" on a new name creates it
    } else if (file->type != NODE_FILE) { // a directory (symlinks were already followed)
        path_ref_free(&ref);
        return NULL;
    }

    Inode *inode = file->data.inode;
    if (mode == REDIR_OUT) // > empties the file now even if the command fails later
        inode_write(inode, NULL, 0, 0);

    // we keep one extra reference while the command runs like an open file
    // so the inode stays alive even if the command removes or moves the file
    inode->ref_count++;

    path_ref_free(&ref);
    return inode;
}

static int process_line(Shell *sh, char *line){ // return 1 stops shell 0: keep going
    // Strip trailing newline if present
    line[strcspn(line, "\r\n")] = '\0';

    Command cmd = {0,NULL,REDIR_NONE,NULL}; // command struct that holds our arguments

    // after stripping \n it calls parse_line 
    if (parse_line(line,&cmd)!=0) // (if written line into shell is not appropriate)
    {  
        print_error();
        return 0;
    }

    if (cmd.argc==0) // we do not have to print anything if no arg given
    {
        command_free(&cmd); // need to free our command struct before returning
        return 0;
    }
    if (strcmp(cmd.argv[0],"exit")==0) // when user writes exit first
    {
        if (cmd.argc==1) // exit must be the first and only argument
        {
            command_free(&cmd);
            return 1; // we exit from the shell with return 1
        } else{
            print_error();
            command_free(&cmd);
            return 0;
        }
    }

    // the redirect target is opened before the command runs
    // so it is found from the current directory and it is created or emptied even if the command fails
    Inode *redir_file = NULL;
    if (cmd.redir != REDIR_NONE) {
        redir_file = open_redirect(sh, cmd.redir, cmd.redir_target);
        if (redir_file == NULL) { // the target can't be a file so the command does not run
            print_error();
            command_free(&cmd);
            return 0;
        }
    }

        CmdFn fn = find_command(cmd.argv[0]); // function corresponds the entered command's function
        if (fn==NULL) // if this command function couldnt be found
        {
            print_error();
            if (redir_file != NULL) // the file stays created but we give back our extra reference
                inode_release(redir_file);
            command_free(&cmd);
            return 0;
        }

        StrBuf out; // we declare string buffer struct
        sb_init(&out); // we initialize it

        int return_code = fn(sh,cmd.argc,cmd.argv,&out); // we run the corresponding function that written in argument
        if (return_code!=0) // if function returns an error
        {
            print_error();
        } else if(redir_file != NULL) {
            // > already emptied the file so the output is always added to the end
            inode_write(redir_file, out.data, out.len, 1);
        } else {
            fwrite(out.data,1,out.len,stdout); // fwrite starts writing from out.data pointer writes chars(each 1 byte) writes out.len times and writes to stdout
        }

        if (redir_file != NULL) // give back the extra reference taken in open_redirect
            inode_release(redir_file);

        // need to free our structs before exiting the function
        sb_free(&out);
        command_free(&cmd);
        return 0;
}

// prints "user@boun: <current path>$ "
static void print_prompt(Shell *sh){
        StrBuf path;
        sb_init(&path);
        dir_path(sh->cwd, &path); // builds the path to display the cwd absolute path
        printf("user@boun: %s$ ", path.data);
        fflush(stdout); // flushed after printing prompt so it renders immediately
        sb_free(&path); // free the path string buffer
}



int main(void)
{
    char line[LINE_BUF_SIZE];

    Shell sh;
    shell_init(&sh); // creates the root directory, cwd = root

    while (1) {
        print_prompt(&sh);

        if (!fgets(line, sizeof line, stdin))
            break;
        if (process_line(&sh,line))
            break;
    }

    shell_destroy(&sh); // frees the whole file system
    return 0;
}
