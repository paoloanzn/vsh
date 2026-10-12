#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "lexer.c"
#include "parser.c"

#ifndef LOG
#ifdef DEBUG
#define LOG(...) printf("debug: "); printf(__VA_ARGS__)
#else
#define LOG(...) do {} while(0)
#endif
#endif

#define PROMPT "vsh %% "
#define CMD_INDEX_SIZE 4096
#define CMD_BUF_SIZE 4096
#define MAX_ARG_LEN 256
#define MAX_ARGS 100

void zero_buf(char *buf, int n) {
    for (int i = 0; i < n; i++)
        buf[i] = '\0';
}

void die(const char *s) {
    printf("error: %s\n", s);
    exit(1);
}

int get_path(char **input, int *idx_out) {
    for (int i = 0; input[i] != NULL; i++) {
        if(strncmp(input[i], "PATH=", 5) == 0) {
            *idx_out = i;
            return i;
        }
    }
    return -1;
}

int exec_file_exists(const char *path) {
    return access(path, F_OK | X_OK);
}

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

    if (paths[0] == '/' && paths[1] == '\0') {
        size = sprintf(buf, "/%s", cmd);
        if (exec_file_exists(buf) < 0) {
            return -1;
        }
        memcpy(abs_path, buf, size + 1);
        return 1;
    }

    if (paths[0] == ':') {
        return -1;
    }

    int last_semicol_seen_idx = -1; 

    for (int i = 0; paths[i] != '\0'; i++) {
        if (paths[i] == ':') {
            memcpy(root_path, paths + last_semicol_seen_idx + 1, i - last_semicol_seen_idx - 1);
            last_semicol_seen_idx = i;
            if (paths[i - 1] == '/') {
                size = sprintf(buf, "%s%s", root_path, cmd);
                if (exec_file_exists(buf) < 0) {
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

#define CHAR_TO_UINT8(c) \
((uint8_t)(c))

typedef enum {
    IO_PIPE = CHAR_TO_UINT8('|'),
    IO_INPUT_REDIRECTION = CHAR_TO_UINT8('<'),
    IO_OUTPUT_REDIRECTION = CHAR_TO_UINT8('>'),

    IO_COUNT = 3
} IoOperation;

bool collect_arguments(char *args_out[], int *args_count, ASTNode **root) {
    ASTNode* current_node = *root;
    while(current_node->num_children > 0) {
        current_node = current_node->children[current_node->num_children-1];
    };

    if ((current_node->num_children == 0) && (current_node->value == NULL))
        return true;

    args_out[*args_count] = current_node->value;
    *args_count += 1;
    if (current_node->parent->right_sibling == NULL)
        return true;

    collect_arguments(args_out, args_count, 
       &current_node->parent->right_sibling);
    return true;
};

int main(int _argc, char **_argv, char **envp) {
    int path_idx;
    if (get_path(envp, &path_idx) < 0) {
        die("PATH is not defined.");
    }

    size_t cmd_buf_size = CMD_BUF_SIZE;
    char cmd_buffer[cmd_buf_size]; 
    char *ptr_cmd_buffer = cmd_buffer;

    for (;;) {
        int read_chars = 0;
        Token tokens[MAX_ARGS+1] = {}; 
        int num_token = 0;

        char cmd_string[4096]; char error_msg[4096] = "";
        char cmd_abs_path[4096];

        char *args[MAX_ARGS+1] = {0};
        int args_count = 0;

        printf(PROMPT);
        read_chars = getline(&ptr_cmd_buffer, &cmd_buf_size, stdin);
        if(read_chars < 0) {
            die("scanf");
        }
 
        if (!get_tokens_for_string(cmd_buffer, read_chars, 
            tokens, error_msg, &num_token)) {
            printf("error: %s\n", error_msg);
            continue;
        }

        ASTNode *root = NULL;
        if (!parse_tokens(tokens, &root, error_msg)) {
            printf("error: %s\n", error_msg);
            delete_tree(root);
            continue;
        }

        #ifdef DEBUG
        print_node(root, 0);
        #endif

        if(!collect_arguments(args, &args_count, &root)) {
            printf("error: ast: failed to parse the tree\n");
            delete_tree(root);
            continue;
        }

        snprintf(cmd_string, MAX_ARG_LEN, "%s", args[0]);

        if (strncmp(cmd_string, "exit", 4) == 0)
            exit(0);

        if (resolve_command_path(envp[path_idx] + 5, 
                cmd_string, cmd_abs_path) < 0) {
            printf("error: %s: command not found\n", cmd_string);
            delete_tree(root);
            continue;
        }
        LOG("ARGS COUNT: %d\n", args_count);
        for (int i = 0; i < args_count; i++) {
            LOG("ARG: value: %s\t size: %lu\n", args[i], strlen(args[i]));
        };

        int pid = fork();
        int child_status;

        if (pid == 0) {
            args[args_count] = NULL;
            execv(cmd_abs_path, args);
        } else {
            wait(&child_status);
            delete_tree(root);
        }
    }
    return 0;
}