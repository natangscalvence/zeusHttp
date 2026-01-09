/**
 * response.c
 * Implements HTTP response header generation and socket writing.
 */

#include "../../include/zeushttp.h"
#include "../../include/core/log.h"
#include "../../include/core/conn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stddef.h> /* For offsetof */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/epoll.h>

extern void close_connection(zeus_conn_t *conn);
extern void start_graceful_close(zeus_conn_t *conn);

/**
 * Sends bytes to a connection, using SSL_write when TLS is active.
 * Note: This functions does NOT modify connection write buffers/offsets.
 */

ssize_t zeus_conn_send(zeus_conn_t *conn, const void *buf, size_t len) {
    if (!conn) {
        return -1;
    }

    if (conn->is_ssl && conn->handshake_done && conn->ssl_conn) {
        int r = SSL_write(conn->ssl_conn, buf, (int)len);
        if (r > 0) {
            return (ssize_t)r;
        }

        int err = SSL_get_error(conn->ssl_conn, r);
        if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
            return 0;   /** would block: caller should wait for EPOLLOUT/EPOLLIN */
        }
        ERR_print_errors_fp(stderr);
        return -1;
    } else {
        ssize_t s = write(conn->event.fd, buf, len);
        if (s >= 0) {
            return s;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        
        perror("write");
        return -1;
    }
}

static void handle_response_write_cb(zeus_io_event_t *ev) {
    zeus_conn_t *conn = (zeus_conn_t *)ev->data;
    if (!conn) return;

    conn_ref(conn);
    
    ZLOG_DEBUG("Write CB: Offset %zu / Total %zu", conn->write_offset, conn->response_len);

    while (conn->write_offset < conn->response_len) {
        ssize_t sent = zeus_conn_send(
            conn,
            conn->response_buffer + conn->write_offset,
            conn->response_len - conn->write_offset
        );

        if (sent > 0) {
            conn->write_offset += (size_t)sent;
            continue;
        }

        if (sent == 0) { 
            conn_unref(conn);
            return;
        }

        ZLOG_WARN("Write error on FD %d", conn->event.fd);
        start_graceful_close(conn);
        conn_unref(conn);
        return;
    }

    ZLOG_INFO("Response sent fully. Initiating close.");
    start_graceful_close(conn); 
    conn_unref(conn);
}


/**
 * Finds the connection structure from the response pointer using
 * offsetof.
 */

static zeus_conn_t* get_conn_from_res(zeus_response_t *res) {
    return (zeus_conn_t*)((char*)res - offsetof(zeus_conn_t, res));
}

/**
 * Returns the standard status message for a given code.
 */

static const char* get_status_message(uint16_t code) {
    switch (code) {
        case 200:
            return "OK";
        case 404:
            return "Not Found";
        case 431:
            return "Request Header Fields Too Large";
        case 500:
            return "Internal Server Error";
        default:
            return "Unknown";
    }
}

/**
 * Sets the HTTP status code for the response.
 */

void zeus_response_set_status(zeus_response_t *res, uint16_t status_code) {
    res->status_code = status_code;
}


/**
 * Adds an HTTP header to the response buffer.
 */

int zeus_response_add_header(zeus_response_t *res, const char *key, const char *value) {
    zeus_conn_t *conn = get_conn_from_res(res);
    int remaining_space = MAX_RESPONSE_BUFFER - conn->response_len;
    int written;

    written = snprintf(conn->response_buffer + conn->response_len, remaining_space,
        "%s: %s\r\n", key, value);

    if (written < 0 || written >= remaining_space) {
        fprintf(stderr, "Error: Response header buffer overflow.\n");
        return -1;
    }
    conn->response_len += written;
    return 0;
}

/**
 * Sends the response headers and data, then close the connection
 * (non-keep-alive).
 */

int zeus_response_send_data(zeus_response_t *res, const char *data, size_t len) {
    zeus_conn_t *conn = get_conn_from_res(res);
    if (!conn || !conn->response_buffer) return -1;

    conn_ref(conn);

    conn->response_len = 0;
    conn->write_offset = 0;

    /** Status line */
    int n = snprintf(
        conn->response_buffer,
        MAX_RESPONSE_BUFFER,
        "HTTP/1.1 %u %s\r\n"
        "Content-Length: %zu\r\n"
        "\r\n",
        res->status_code,
        get_status_message(res->status_code),
        len
    );

    if (n <= 0 || (size_t)n >= MAX_RESPONSE_BUFFER) {
        conn_unref(conn);
        return -1;
    }

    conn->response_len = (size_t)n;

    /** Append body */
    if (len > 0) {
        if (conn->response_len + len > MAX_RESPONSE_BUFFER) {
            conn_unref(conn);
            return -1;
        }

        memcpy(conn->response_buffer + conn->response_len, data, len);
        conn->response_len += len;
    }

    /** Try send */
    ssize_t sent = zeus_conn_send(conn, conn->response_buffer, conn->response_len);
    if (sent < 0) {
        start_graceful_close(conn);
        conn_unref(conn);
        return -1;
    }

    conn->event.write_cb = handle_response_write_cb;

    zeus_event_ctl(
        conn->server,
        &conn->event, 
        EPOLL_CTL_MOD,
        EPOLLOUT | EPOLLET
    );

    conn_unref(conn);
    return 0;
}

/**

int zeus_response_send_file(zeus_response_t *res, const char *filepath) {
    zeus_conn_t *conn = get_conn_from_res(res);
    if (!conn) {
        return -1;
    }

    int file_fd = open(filepath, O_RDONLY);
    if (file_fd < 0) {
        ZLOG_PERROR("Response: Failed to open file '%s'", filepath);
        return -1;
    }


    struct stat st;
    if (fstat(file_fd, &st) < 0) {
        ZLOG_PERROR("Response: Failed to stat file '%s'", filepath);
        close(file_fd);
        return -1;
    }

    conn->sendfile_fd = file_fd;
    conn->sendfile_size = st.st_size;
    conn->sendfile_offset = 0;
    conn->is_sending_file = 1;


    conn->event.write_cb = handle_response_write_cb;
    if (zeus_event_ctl(conn->server, &conn->event, EPOLL_CTL_MOD, EPOLLOUT | EPOLLET) == -1) {
        ZLOG_PERROR("epoll_ctl (sendfile start) failed.");
        close(file_fd);
        return -1;
    }
    return 0;
}
*/


