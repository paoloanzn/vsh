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

#define MAXCONN 100

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
typedef enum {
    STATUS_NOT_STARTED,
    STATUS_STARTED,
    STATUS_FAILED
} ServerStatus;

typedef struct {
    pthread_cond_t cond;
    pthread_mutex_t mtx;
    ServerStatus server_status;
} ServerThreadSync;

void *start_server(void *arg) {
    ServerThreadSync *sync = (ServerThreadSync *)arg;

    char err_msg[4096] = "";

    pid_t pid = getpid();
    int port = 10000 + ((int)pid % 55536);
    int socket;
    int client_socket;
    struct sockaddr_in client_addr;
    unsigned int len = sizeof(client_addr);

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