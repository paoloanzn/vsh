#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "lexer.c"
#include "parser.c"

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
        char cmd_abs_path[4096]; char *args[MAX_ARGS+1];
        char arg_bufs[MAX_ARGS+1][MAX_ARG_LEN];

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

        if (!parse_tokens(tokens, NULL, error_msg)) {
            printf("error: %s\n", error_msg);
            continue;
        }

        Token *cmd = &tokens[0];
        snprintf(cmd_string, cmd->len, "%s", cmd->value);

        switch (cmd->type) {
            case TOKEN_EMPTY:
                continue;
            case TOKEN_IO:
            case TOKEN_LITERAL:
                printf("error: %s\n", "invalid syntax.");
            default:
                break;
        }

        if (strncmp(cmd_string, "exit", 4) == 0)
            exit(0);

        if (resolve_command_path(envp[path_idx] + 5, 
                cmd_string, cmd_abs_path) < 0) {
            printf("error: %s: command not found\n", cmd_string);
            continue;
        }

        args[0] = cmd_string;
        for (int i = 1; i < num_token; i++) {
            size_t n = tokens[i].len < MAX_ARG_LEN ? tokens[i].len : MAX_ARG_LEN;
            snprintf(arg_bufs[i], n, "%s", tokens[i].value);
            args[i] = arg_bufs[i];
        }

        int pid = fork();
        int child_status;

        if (pid == 0) {
            args[num_token]= NULL;
            execv(cmd_abs_path, args);
        } else {
            wait(&child_status);
        }
    }
    return 0;
}