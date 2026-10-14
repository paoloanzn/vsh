// Signal masking helpers. The shell blocks SIGINT so that Ctrl-C does not
// kill it; child processes unblock it so that Ctrl-C still stops the running
// command.

#include <bits/types/sigset_t.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>

// Writes a formatted error message into `err`. 
#ifndef FORMAT_ERROR
#define FORMAT_ERROR(format, err, ...) \
snprintf(err, 4096, format, ##__VA_ARGS__)
#endif

// Empties `set`, then adds the `len` signals listed in `blocked` to it.
// On failure, writes the reason into `err_msg` and returns false.
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

// Adds the signals in `set` to the calling thread's signal mask, so they
// stay pending instead of being delivered. Returns false on failure.
bool block_signal_set(sigset_t *set, char *err_msg) {
    if(sigprocmask(SIG_BLOCK, set, NULL) < 0) {
        FORMAT_ERROR("signal: failed to block signals", err_msg);
        return false;
    }
    return true;
}

// Removes the signals in `set` from the calling thread's signal mask, so
// they are delivered again. Returns false on failure.
bool unblock_signal_set(sigset_t *set, char *err_msg) {
    if(sigprocmask(SIG_UNBLOCK, set, NULL) < 0) {
        FORMAT_ERROR("signal: failed to unblock signals", err_msg);
        return false;
    }
    return true;
}

// Blocks SIGINT for the calling thread. Threads created afterwards and
// children made by fork() inherit the mask. Returns false on failure.
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

// Unblocks SIGINT for the calling thread. Undoes block_sigint().
// Returns false on failure.
bool unblock_sigint(char *err_msg) {
    sigset_t set = {};
    int to_unblock[] = {SIGINT};

    if(!init_signal_set(&set, to_unblock, 1, 
        err_msg)) {
        return false;
    }
    if(!unblock_signal_set(&set, err_msg)) {
        return false;
    }

    return true;
}