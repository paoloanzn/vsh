#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

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

    if (paths[0] == '/' && paths[1] == '\0') {
        size = sprintf(buf, "/%s", cmd);
        if (exec_file_exists(buf) < 0) {
            return -1;
        }
        memcpy(abs_path, buf, size);
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
                memcpy(abs_path, buf, size);
                return 1;
            }

            size = sprintf(buf, "%s/%s", root_path, cmd);
            if (exec_file_exists(buf) < 0) {
                zero_buf(root_path, 4096);
                continue;
            }
            memcpy(abs_path, buf, size);
            return 1;
        }
    }

    return -1;
}

// TODO: implement ' handling.
int parse_input(const char *input_string, size_t input_len, char argv[][MAX_ARG_LEN]) {
    char read_buffer[MAX_ARG_LEN];
    int seen_chars = 0;
    int argn = 0;
    char separator_token = ' ';

    for (int i = 0; i < input_len - 1; i++) {
        if (input_string[i] == separator_token) {
            if (seen_chars == 0)
                continue;
            if (input_string[i] == '"' && separator_token == '"') {
                separator_token = ' ';
                read_buffer[seen_chars + 1] = '\0';
                memcpy(argv + argn, read_buffer + 1, seen_chars - 1);
            } else {
                read_buffer[seen_chars + 1] = '\0';
                memcpy(argv + argn, read_buffer, seen_chars);
            }
            seen_chars = 0;
            argn++;
            continue;
        }

        if (input_string[i] == '"')
            separator_token = '"';
        read_buffer[seen_chars] = input_string[i];
        seen_chars++;

        if (i == input_len - 2) {
            read_buffer[seen_chars + 1] = '\0';
            memcpy(argv + argn, read_buffer, seen_chars);
            argn++;
            continue;
        }
    }

    return argn;
}

int main(int _argc, char **_argv, char **envp) {
    int path_idx;
    if (get_path(envp, &path_idx) < 0) {
        die("PATH is not defined.");
    }

    size_t cmd_buf_size = CMD_BUF_SIZE;
    char cmd_buffer[4096];
    char *ptr_cmd_buffer = cmd_buffer;
    int read_chars = 0;
    int argn;
    char cmd_path[4096];
    zero_buf(cmd_path, 4096);

    for (;;) {
        char argv[MAX_ARGS][MAX_ARG_LEN];

        printf(PROMPT);
        read_chars = getline(&ptr_cmd_buffer, &cmd_buf_size, stdin);
        if(read_chars < 0) {
            die("scanf");
        }
        argn = parse_input(cmd_buffer, read_chars, argv);

        if (argn == 0) {
            continue;
        }

        if (strncmp(argv[0], "exit", 4) == 0)
            exit(0);

        if (resolve_command_path(envp[path_idx] + 5, argv[0], cmd_path) < 0) {
            printf("error: %s: command not found\n", argv[0]);
            for (int i = 0; i < argn; i++) {
                zero_buf(argv[i], MAX_ARG_LEN);
            }
            continue;
        }

        int pid = fork();
        int child_status;

        if (pid == 0) {
            char *exec_argv[MAX_ARGS + 1];
            for (int i = 0; i < argn; i++)
                exec_argv[i] = argv[i];
            exec_argv[argn] = NULL;
            execv(cmd_path, exec_argv);
        } else {
            wait(&child_status);
        }


        for (int i = 0; i < argn; i++) {
            zero_buf(argv[i], MAX_ARG_LEN);
        }
    }
    return 0;
}