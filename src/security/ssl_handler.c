/**
 * ssl_handler.c
 * Manages the non-blocking SSL handshake state for connections.
 */

#include "../../include/zeushttp.h"
#include "../../include/core/conn.h"
#include <stdio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <errno.h>

#include <sys/socket.h>

/**
 * TODO: Define kqueue for BSD systems.
 */

#include <sys/epoll.h>

extern int zeus_event_ctl(zeus_server_t *server, zeus_io_event_t *ev, int op, uint32_t events);
extern void handle_write_cb(zeus_io_event_t *ev);
extern void close_connection(zeus_conn_t *conn);

/**
 * Try to continue the TLS handshake (SSL_accept) in a non-blocking
 * manner.
 */

int zeus_handle_ssl_handshake(zeus_conn_t *conn) {
    if (!conn) return -1;
    if (!conn->ssl_conn) {
        fprintf(stderr, "[SSL HANDSHAKE] no ssl object for FD %d\n", conn->event.fd);
        return -1;
    }

    /** 
     * Ensure OpenSSL BIOs operate in non-blocking mode (best-effort)
     */
    
    {
        BIO *rbio = SSL_get_rbio(conn->ssl_conn);
        BIO *wbio = SSL_get_wbio(conn->ssl_conn);
        if (rbio) BIO_set_nbio(rbio, 1);
        if (wbio) BIO_set_nbio(wbio, 1);
    }

    int ret = SSL_accept(conn->ssl_conn);

    /** 
     * Handshake completed synchronously 
     */

    if (ret == 1) {
        conn->handshake_done = 1;
        conn->event.write_cb = NULL;    /** handshake write not needed anymore */

        const unsigned char *alpn_proto = NULL;
        unsigned int alpn_len = 0;
        SSL_get0_alpn_selected(conn->ssl_conn, &alpn_proto, &alpn_len);

        if (alpn_proto && alpn_len > 0) {
            printf("[SSL] Handshake OK FD=%d ALPN=%.*s\n",
                   conn->event.fd, alpn_len, (const char *)alpn_proto);
        } else {
            printf("[SSL] Handshake OK FD=%d ALPN=none\n", conn->event.fd);
        }

        printf("[SSL] Protocol: %s\n", SSL_get_version(conn->ssl_conn));

        if (zeus_event_ctl(conn->server, &conn->event, EPOLL_CTL_MOD, EPOLLIN | EPOLLET) == -1) {
            perror("epoll_ctl (post-handshake) failed");
            close_connection(conn);
            return -1;
        }

        return 1;
    }

    /** Handshake in progress or error */
    int err = SSL_get_error(conn->ssl_conn, ret);

    switch (err) {
        case SSL_ERROR_WANT_READ:
            if (zeus_event_ctl(conn->server, &conn->event, EPOLL_CTL_MOD, EPOLLIN | EPOLLET) == -1) {
                perror("epoll_ctl WANT_READ failed");
                close_connection(conn);
                return -1;
            }
            return 0;

        case SSL_ERROR_WANT_WRITE:
            conn->event.write_cb = handle_write_cb;
            if (zeus_event_ctl(conn->server, &conn->event, EPOLL_CTL_MOD, EPOLLOUT | EPOLLET) == -1) {
                perror("epoll_ctl WANT_WRITE failed");
                close_connection(conn);
                return -1;
            }
            return 0;

        case SSL_ERROR_ZERO_RETURN:
            fprintf(stderr, "[SSL] Handshake: peer closed cleanly FD=%d\n", conn->event.fd);
            close_connection(conn);
            return -1;

        case SSL_ERROR_SYSCALL:
            fprintf(stderr, "[SSL] Handshake SYSCALL error FD=%d errno=%d (%s)\n",
                    conn->event.fd, errno, strerror(errno));
            close_connection(conn);
            return -1;

        default:
            ERR_print_errors_fp(stderr);
            fprintf(stderr, "[SSL] Handshake fatal FD=%d code=%d\n", conn->event.fd, err);
            close_connection(conn);
            return -1;
    }
}

/**
 * Write callback used during handshake when OpenSSL requests a writable socket.
 * It simply retries the handshake state machine until it completes or fails.
 *
 * Note: ev->data is expected to be zeus_conn_t* for connection FDs.
 */

void handle_write_cb(zeus_io_event_t *ev) {
    if (!ev) return;
    zeus_conn_t *conn = (zeus_conn_t *)ev->data;
    conn_ref(conn);
    if (!conn) return;

    if (!conn->ssl_conn) {
        return;
    }

    if (conn->handshake_done) {
        conn->event.write_cb = NULL;
        zeus_event_ctl(conn->server, &conn->event, EPOLL_CTL_MOD, EPOLLIN | EPOLLET);
        return;
    }

    /** Continue handshake */
    int hs_result = zeus_handle_ssl_handshake(conn);

    if (hs_result < 0) {
        return;
    }

    if (hs_result == 0) {
        /** 
         * handshake still in progress 
         */
        return;
    }
    conn_unref(conn);
}



/**
 * Initialize a graceful close for TLS connections.
 */

void start_graceful_close(zeus_conn_t *conn) {
    if (conn->closing) {
        return;
    }

    conn->closing = 1;

#ifdef __linux__
    zeus_event_ctl(conn->server, &conn->event, EPOLL_CTL_DEL, 0);
#endif 

    shutdown(conn->event.fd, SHUT_RDWR);
    close(conn->event.fd);
    conn_unref(conn);
}