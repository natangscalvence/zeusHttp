/**
 * response.c
 * Implements HTTP response header generation and socket writing.
 */

#include "../../include/zeushttp.h"
#include "../../include/core/conn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stddef.h> /* For offsetof */

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

void handle_response_write_cb(zeus_io_event_t *ev) {
    if (!ev) {
        return;
    }

    zeus_conn_t *conn = (zeus_conn_t *)ev->data;

    if (!conn) {
        return;
    }

    /**
     * Send remaining header/body.
     */

     while (conn->write_offset < conn->response_len) {
        ssize_t r = zeus_conn_send(conn, 
            conn->response_buffer + conn->write_offset, conn->response_len -
            conn->write_offset);

        if (r > 0) {
            conn->write_offset += (size_t)r;
            continue;
        } else if (r == 0) {
            if (zeus_event_ctl(conn->server, &conn->event, EPOLL_CTL_MOD, EPOLLOUT | EPOLLET) == -1) {
                perror("epoll_ctl mod (response write) failed");
                break;
            }
            return;
        } else {
            start_graceful_close(conn);
            return;
        }
     }

    /** 
     * Finished sending everything.
     */

    conn->write_offset = 0;
    conn->response_len = 0;

    start_graceful_close(conn);
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
    if (!conn) return -1;

    /**
     * Ensure that buffers exists.
     */

    if (!conn->response_buffer) {
        fprintf(stderr, "No response buffer on conn\n");
        return -1;
    }

    /**
     * Build Content-Lenght and CRLF safely.
     */

    int remaining = MAX_RESPONSE_BUFFER - (int)conn->response_len;
    if (remaining <= 0) {
        fprintf(stderr, "Response buffer has no remaining space\n");
        return -1;
    }

    int hdr_written = snprintf(conn->response_buffer + conn->response_len,
                               (size_t)remaining,
                               "Content-Length: %zu\r\n\r\n", len);

    if (hdr_written < 0 || hdr_written >= remaining) {
        fprintf(stderr, "Error: Response header buffer overflow (content-length)\n");
        return -1;
    }
    conn->response_len += (size_t)hdr_written;

    char status_line_buf[128];
    const char *status_msg = get_status_message(res->status_code);
    int status_line_len = snprintf(status_line_buf, sizeof(status_line_buf),
                                   "HTTP/1.1 %u %s\r\n", res->status_code, status_msg);
    if (status_line_len < 0 || status_line_len >= (int)sizeof(status_line_buf)) {
        fprintf(stderr, "Status line buffer overflow\n");
        return -1;
    }

    /** Ensure we have space to prepend status line */
    if ((size_t)status_line_len + conn->response_len > MAX_RESPONSE_BUFFER) {
        fprintf(stderr, "Not enough space for status line + headers\n");
        return -1;
    }

    /** Shift headers right to make space for status-line (memmove handles overlap) */
    memmove(conn->response_buffer + status_line_len,
            conn->response_buffer, conn->response_len);

    memcpy(conn->response_buffer, status_line_buf, (size_t)status_line_len);
    conn->response_len += (size_t)status_line_len;

    conn->write_offset = 0; 
    ssize_t sent = zeus_conn_send(conn, conn->response_buffer, conn->response_len);
    if (sent < 0) {
        start_graceful_close(conn);
        return -1;
    }
    if ((size_t)sent < conn->response_len) {
        /** partial send -> set offset and arm EPOLLOUT */
        conn->write_offset = (size_t)sent;
        conn->event.write_cb = handle_response_write_cb;
        if (zeus_event_ctl(conn->server, &conn->event, EPOLL_CTL_MOD, EPOLLOUT | EPOLLET) == -1) {
            perror("epoll_ctl mod (response partial) failed");
            start_graceful_close(conn);
            return -1;
        }

        /**
         * schedule body to be sent after headers; append body to a separate buffer or
         * in this simplified version we will attempt to send body now via same flow.
         */

        if (len > 0) {
            ssize_t body_sent = zeus_conn_send(conn, data, len);
            if (body_sent < 0) {
                start_graceful_close(conn);
                return -1;
            }
            if ((size_t)body_sent < len) {
                size_t remain = len - (size_t)body_sent;
                if (conn->response_len + remain > MAX_RESPONSE_BUFFER) {
                    fprintf(stderr, "Not enough buffer space to queue body remainder\n");
                    start_graceful_close(conn);
                    return -1;
                }
                memcpy(conn->response_buffer + conn->response_len, data + body_sent, remain);
                conn->response_len += remain;
            } else {
                /** body fully sent (headers already queued). */
            }
        }
        return (int)sent;
    }

    /**
     * Headers fully sent. Try sending body immediately 
     */

    if (len > 0) {
        ssize_t body_sent = zeus_conn_send(conn, data, len);
        if (body_sent < 0) {
            start_graceful_close(conn);
            return -1;
        }
        if ((size_t)body_sent < len) {
            /** append remainder to response buffer */
            size_t remain = len - (size_t)body_sent;
            if (conn->response_len + remain > MAX_RESPONSE_BUFFER) {
                fprintf(stderr, "Not enough buffer space to queue body remainder\n");
                start_graceful_close(conn);
                return -1;
            }
            memcpy(conn->response_buffer + conn->response_len, data + body_sent, remain);
            conn->response_len += remain;
            conn->write_offset = conn->response_len - remain; 
            conn->event.write_cb = handle_response_write_cb;
            if (zeus_event_ctl(conn->server, &conn->event, EPOLL_CTL_MOD, EPOLLOUT | EPOLLET) == -1) {
                perror("epoll_ctl mod (response body partial) failed");
                start_graceful_close(conn);
                return -1;
            }
            return (int)(status_line_len + hdr_written + body_sent);
        }
    }

    /** Everything sent synchronously, close gracefully :D */
    
    start_graceful_close(conn);
    return (int)(conn->response_len + len);
}

