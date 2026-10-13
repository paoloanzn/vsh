#include <bits/types/sigset_t.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>

// Writes a formatted error message into `err`. 
#ifndef FORMAT_ERROR
#define FORMAT_ERROR(format, err, ...) \
snprintf(err, 4096, format, ##__VA_ARGS__)
#endif

bool init_signal_set(sigset_t *set, int blocked[], size_t len, char *err_msg){
    if(sigemptyset(set) < 0) {
        FORMAT_ERROR("signal: failed to init set", err_msg);
        return false;
    }

    for (int i = 0; i < len; i++) {
        if(sigaddset(set, blocked[i]) < 0) {
            FORMAT_ERROR("signal: failed to add signal: %d; \
                stopped adding", 
                err_msg, blocked[i]);
            return false;
        }
    }
    return true;
}

bool block_signal_set(sigset_t *set, char *err_msg) {
    if(sigprocmask(SIG_BLOCK, set, NULL) < 0) {
        FORMAT_ERROR("signal: failed to block signals", err_msg);
        return false;
    }
    return true;
}

bool block_sigint(char *err_msg) {
    sigset_t set = {};
    int to_block[] = {SIGINT};

    if(!init_signal_set(&set, to_block, 1, 
        err_msg)) {
        return false;
    }
    if(!block_signal_set(&set, err_msg)) {
        return false;
    }

    return true;
}