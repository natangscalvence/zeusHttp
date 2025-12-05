/**
 * includes/core/server.h
 * Defines the complete internal structure for zeusHttp master server.
 */

#ifndef ZEUS_SERVER_H
#define ZEUS_SERVER_H

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdint.h>
#include <stddef.h>

/**
 * This is the public facing type, defind fully below.
 */
typedef struct zeus_server zeus_server_t;

/**
 * Holds all global state shared or managed by the master process.
 */

struct zeus_server {
    int listen_fd;      /** The file descriptor for the listeing socket. */
    int loop_fd;        /** The file descriptor for the epoll/kqueue instance. */
    SSL_CTX *ssl_ctx;   /** The global TLS context (shared among worker.) */
};

#endif // ZEUS_SERVER_H