// vsh: a minimal shell.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "lexer.c"
#include "parser.c"

// Debug logging: prints only when compiled with -DDEBUG (see compile.sh).
#ifndef LOG
#ifdef DEBUG
#define LOG(...) printf("debug: "); printf(__VA_ARGS__)
#else
#define LOG(...) do {} while(0)
#endif
#endif

// Writes a formatted error message into `err`. 
#ifndef FORMAT_ERROR
#define FORMAT_ERROR(format, err, ...) \
snprintf(err, 4096, format, ##__VA_ARGS__)
#endif

#define PROMPT "vsh %% "
// Initial size of the buffer that holds the input line.
#define CMD_BUF_SIZE 4096
// Maximum length of the command name, NUL included.
#define MAX_CMD_LEN 256
// Maximum number of tokens and of arguments in a command line.
#define MAX_ARGS 100

// Sets the first `n` bytes of `buf` to NUL.
void zero_buf(char *buf, int n) {
    for (int i = 0; i < n; i++)
        buf[i] = '\0';
}

// Prints an error message and terminates the shell.
void die(const char *s) {
    printf("error: %s\n", s);
    exit(1);
}

// Looks for the "PATH=..." entry in the NULL-terminated environment `input`.
// Stores its index in `*idx_out` and returns it, or returns -1 if missing.
int get_path(char **input, int *idx_out) {
    for (int i = 0; input[i] != NULL; i++) {
        if(strncmp(input[i], "PATH=", 5) == 0) {
            *idx_out = i;
            return i;
        }
    }
    return -1;
}

// Returns 0 if `path` exists and is executable, -1 otherwise.
int exec_file_exists(const char *path) {
    return access(path, F_OK | X_OK);
}

bool is_directory(const char *path) {
    struct stat path_stat;
    if(stat(path, &path_stat) != 0) {
        // We assume path existence and accessibility
        // have been already checked.
        die("error: can't read path stats");
    }
    if(S_ISDIR(path_stat.st_mode))
        return true;
    return false;
}

bool can_write_at_path(const char *path, char *err_msg) {
    // Check if path exists (i.e file or directory)
    bool path_exists = true;
    if (access(path, F_OK) < 0)
        path_exists = false;

    // If exists check if is a directory
    if (path_exists && is_directory(path)) {
        FORMAT_ERROR("'%s' is a directory", err_msg, path);
        return false;
    }

    // If exists check it's writable
    if (path_exists && (access(path, W_OK) < 0)) {
        FORMAT_ERROR("can't write at path: '%s'", 
            err_msg, path);
        return false;
    }

    return true;
}

// Finds the executable for `cmd` and writes its path into `abs_path`.
// `paths` is the value of PATH: a list of directories separated by ':'.
// Returns 1 on success, -1 if no executable is found.
int resolve_command_path(char *paths, char *cmd, char *abs_path) {
    int size;
    char buf[4096]; char root_path[4096];

    // A command containing a slash is a path: use it as-is, don't search PATH.
    if (strchr(cmd, '/') != NULL) {
        if (exec_file_exists(cmd) < 0) {
            return -1;
        }
        size = strlen(cmd);
        memcpy(abs_path, cmd, size + 1);
        return 1;
    }

    // PATH is just "/": look in the root directory only.
    if (paths[0] == '/' && paths[1] == '\0') {
        size = sprintf(buf, "/%s", cmd);
        if (exec_file_exists(buf) < 0) {
            return -1;
        }
        memcpy(abs_path, buf, size + 1);
        return 1;
    }

    // PATH starting with an empty entry is not supported.
    if (paths[0] == ':') {
        return -1;
    }

    // Index of the last ':' seen; -1 so the first entry starts at index 0.
    int last_semicol_seen_idx = -1; 

    for (int i = 0; paths[i] != '\0'; i++) {
        // Each ':' ends a directory entry: try `cmd` inside that directory.
        if (paths[i] == ':') {
            memcpy(root_path, paths + last_semicol_seen_idx + 1, i - last_semicol_seen_idx - 1);
            last_semicol_seen_idx = i;
            // The entry already ends with '/': don't add another one.
            if (paths[i - 1] == '/') {
                size = sprintf(buf, "%s%s", root_path, cmd);
                if (exec_file_exists(buf) < 0) {
                    // Clear the entry so the next, possibly shorter, one
                    // copied over it is NUL-terminated.
                    zero_buf(root_path, 4096);
                    continue;
                }
                memcpy(abs_path, buf, size + 1);
                return 1;
            }

            size = sprintf(buf, "%s/%s", root_path, cmd);
            if (exec_file_exists(buf) < 0) {
                zero_buf(root_path, 4096);
                continue;
            }
            memcpy(abs_path, buf, size + 1);
            return 1;
        }
    }

    return -1;
}

// Casts a character to its unsigned byte value.
#define CHAR_TO_UINT8(c) \
((uint8_t)(c))

// I/O operators; each value is the operator's character byte repr.
typedef enum {
    IO_PIPE = CHAR_TO_UINT8('|'),
    IO_INPUT_REDIRECTION = CHAR_TO_UINT8('<'),
    IO_OUTPUT_REDIRECTION = CHAR_TO_UINT8('>'),

    IO_COUNT = 3
} IoOperation;

// Collects the values of the AST leaves into `args_out`, left to right,
// incrementing `*args_count` for each one. Values are not copied: they stay
// owned by the tree. Starts from `*root` and stops at the first leaf with no
// value (_epsilon_), i.e. at the end of the program.
bool collect_arguments(char *args_out[], int *args_count, 
    ASTNode **root, int *io_last_idx) {
    // Descend to the leftmost leaf; the last child is the leftmost symbol
    // (see ASTNode.children).
    ASTNode* current_node = *root;
    while(current_node->num_children > 0) {
        current_node = current_node->children[current_node->num_children-1];
    };

    if ((current_node->num_children == 0) && (current_node->value == NULL))
        return true;

    // A leaf with a value is a word, literal or io token: store it.
    args_out[*args_count] = current_node->value;
    *args_count += 1;
    
    // Stores the args_count's index of the last found IO symbol. 
    if (strncmp(current_node->label, 
        gSymbolsLiteralsReferenceTable[SYMBOL_IO], 2) == 0)
        *io_last_idx = *args_count - 1;
    
    // Walk up from the leaf to the first node with a right sibling. 
    ASTNode *next = current_node;
    while (next != NULL && next->right_sibling == NULL)
        next = next->parent;
    if (next == NULL)
        return true;

    collect_arguments(args_out, args_count,
       &next->right_sibling, io_last_idx);
    return true;
};

// Read-eval loop: reads a line, tokenizes and parses it, then runs it in a
// child process. Exits on the "exit" command or when input can't be read.
int main(int _argc, char **_argv, char **envp) {
    int path_idx;
    if (get_path(envp, &path_idx) < 0) {
        die("PATH is not defined.");
    }

    // Buffer for the input line, passed to getline.
    size_t cmd_buf_size = CMD_BUF_SIZE;
    char cmd_buffer[cmd_buf_size]; 
    char *ptr_cmd_buffer = cmd_buffer;

    for (;;) {
        int read_chars = 0;
        // Zero-initialized, so the entry after the last token has type
        // TOKEN_EMPTY, which the parser reads as end of input.
        Token tokens[MAX_ARGS+1] = {}; 
        int num_token = 0;

        char cmd_string[4096]; char error_msg[4096] = "";
        char cmd_abs_path[4096];

        // Arguments for execv; the extra slot is for the closing NULL.
        char *args[MAX_ARGS+1] = {0};
        int args_count = 0;
        int io_idx = -1;

        printf(PROMPT);
        // Fails on read errors and at end of input (e.g. Ctrl-D).
        read_chars = getline(&ptr_cmd_buffer, &cmd_buf_size, stdin);
        if(read_chars < 0) {
            die("scanf");
        }
 
        // Lex: input line -> tokens.
        if (!get_tokens_for_string(cmd_buffer, read_chars, 
            tokens, error_msg, &num_token)) {
            printf("error: %s\n", error_msg);
            continue;
        }

        // Empty input, nothing to execute.
        if (tokens[0].type == TOKEN_EMPTY)
            continue;

        // Parse: tokens -> AST.
        ASTNode *root = NULL;
        if (!parse_tokens(tokens, &root, error_msg)) {
            printf("error: %s\n", error_msg);
            delete_tree(root);
            continue;
        }

        #ifdef DEBUG
        print_node(root, 0);
        #endif

        // AST -> argument list; args[0] is the command name.
        if(!collect_arguments(args, &args_count, 
            &root, &io_idx)) {
            printf("error: ast: failed to parse the tree\n");
            delete_tree(root);
            continue;
        }

        snprintf(cmd_string, MAX_CMD_LEN, "%s", args[0]);

        // "exit" is not a command; when received we terminate the shell proc.
        if (strncmp(cmd_string, "exit", 4) == 0)
            exit(0);

        // `+ 5` skips the "PATH=" prefix.
        if (resolve_command_path(envp[path_idx] + 5, 
                cmd_string, cmd_abs_path) < 0) {
            printf("error: %s: command not found\n", cmd_string);
            delete_tree(root);
            continue;
        }
        #ifdef DEBUG
        LOG("ARGS COUNT: %d\n", args_count);
        for (int i = 0; i < args_count; i++) {
            LOG("ARG: value: %s\t size: %lu\n", args[i], strlen(args[i]));
        };
        #endif
        // Run the command in a child process and wait for it to finish.
        int pid = fork();
        int child_status;

        if (pid == 0) {
            if (io_idx > 0) {
                LOG("IO: io token %c found at index %d\n", 
                    CHAR_TO_UINT8(*args[io_idx]), io_idx);
                switch (CHAR_TO_UINT8(*args[io_idx])) {
                    case IO_PIPE:
                    case IO_INPUT_REDIRECTION:
                    case IO_OUTPUT_REDIRECTION:
                    LOG("IO: Output redirection to %s\n", args[io_idx+1]);
                    // The next argument is the dest of the output redirection.
                    if (!can_write_at_path(args[io_idx+1], 
                        error_msg)) {
                        printf("error: %s\n", error_msg);
                        exit(1);
                    }

                    int out_fd = open(args[io_idx+1], 
                        O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (out_fd < 0) {
                        printf("error: failed to create or open '%s'\n", 
                            args[io_idx+1]);
                        exit(1);
                    }
                    dup2(out_fd, STDOUT_FILENO);
                    close(out_fd);

                    default:
                    break;
                }
            }

            if (io_idx > 0) args[io_idx] = NULL;
            else args[args_count] = NULL;
            execv(cmd_abs_path, args);
        } else {
            wait(&child_status);
            if (child_status != 0)
                printf("[process %d exited with status %d]\n", 
                pid, child_status);
            delete_tree(root);
        }
    }
    return 0;
}