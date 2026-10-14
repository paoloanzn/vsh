// Spy server: a TCP server that runs on its own thread next to the shell and
// streams over the commands ran by it. 
// 
// A client can connect to it by running the following command: 
// `telnet localhost PORT`
// PORT can be obtained using `./get-port.sh`.

#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

// Writes a formatted error message into `err`. 
#ifndef FORMAT_ERROR
#define FORMAT_ERROR(format, err, ...) \
snprintf(err, 4096, format, ##__VA_ARGS__)
#endif

// Debug logging: prints only when compiled with -DDEBUG (see compile.sh).
#ifndef LOG
#ifdef DEBUG
#define LOG(...) printf("debug: "); printf(__VA_ARGS__)
#else
#define LOG(...) do {} while(0)
#endif
#endif

// Maximum number of pending connections waiting to be accepted (listen()
// backlog).
#define MAXCONN 100

// Creates a TCP socket listening on `port` on every local IPv4 address and
// stores it in `*socket_out`. On failure, writes the reason into `err_msg`
// and returns false.
bool create_socket(int port, int *socket_out, char *err_msg) {
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    *socket_out = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (*socket_out < 0) {
        FORMAT_ERROR("spy: unable to create socket", err_msg);
        return false;
    }

    if (bind(*socket_out, (struct sockaddr*)&addr, 
    sizeof(addr)) < 0) {
        FORMAT_ERROR("spy: unable to bind socket", err_msg);
        return false;
    }

    if (listen(*socket_out, MAXCONN) < 0) {
        FORMAT_ERROR("spy: unable to listen", err_msg);
        return false;
    }

    return true;
}

// Startup state of the server thread, reported back to the main thread.
typedef enum {
    // The thread has not tried to open its socket yet.
    STATUS_NOT_STARTED,
    // The socket is listening.
    STATUS_STARTED,
    // The socket could not be created; the thread has exited.
    STATUS_FAILED
} ServerStatus;

// Shared between the main thread and the server thread. The main thread
// waits on `cond` until the server thread moves `server_status` away from
// STATUS_NOT_STARTED. `mtx` guards `server_status`.
typedef struct {
    pthread_cond_t cond;
    pthread_mutex_t mtx;
    ServerStatus server_status;
} ServerThreadSync;

// Server thread entry point; `arg` is a ServerThreadSync. Opens the listening
// socket, reports the outcome through `arg`, then serves clients one at a
// time until the thread is cancelled.
void *start_server(void *arg) {
    ServerThreadSync *sync = (ServerThreadSync *)arg;

    char err_msg[4096] = "";

    pid_t pid = getpid();
    // The port is derived from the shell's PID, so that several shells can
    // run at once; get-port.sh computes the same value. The result is in
    // 10000-65535, avoid the privileged ports range (below 1024).
    int port = 10000 + ((int)pid % 55536);
    int socket;
    int client_socket;
    struct sockaddr_in client_addr;
    unsigned int len = sizeof(client_addr);

    // Report the outcome, success or failure, to the waiting main thread.
    pthread_mutex_lock(&sync->mtx);
    if(!create_socket(port, &socket, err_msg)) {
        sync->server_status = STATUS_FAILED;
        pthread_cond_signal(&sync->cond);
        pthread_mutex_unlock(&sync->mtx);
        printf("server_thread: %s\n", err_msg);
        pthread_exit(NULL);
    }
    sync->server_status = STATUS_STARTED;
    pthread_cond_signal(&sync->cond);
    pthread_mutex_unlock(&sync->mtx);

    LOG("Server listening on port %d\n", port);

    // Server loop
    while ((client_socket = accept(socket, (struct sockaddr*)&client_addr, &len))) {
        LOG("Client connected\n");
        const char *reply = "Hello!\n";
        send(client_socket, reply, strlen(reply), 0);
        close(client_socket);
    }

    pthread_exit(NULL);
}