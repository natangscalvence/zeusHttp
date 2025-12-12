#include "../../include/zeushttp.h"
#include "../../include/http/http.h"
#include "../../include/core/conn.h"
#include "../../include/core/server.h"
#include "../../include/core/io_event.h"
#include "../../include/core/worker_signals.h"
#include "../../include/core/log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#ifdef __linux__
#include <sys/epoll.h>
#define ZEUS_MAX_EVENTS 128
#define ZEUS_EVENT_LOOP_ID int
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>

extern int zeus_handle_ssl_handshake(zeus_conn_t *conn);
extern void handle_write_cb(zeus_io_event_t *ev);
extern int zeus_drop_privileges();


/**
 * Security limits, adjustable.
 */

 #define MAX_HEADERS_LEN 8192       /** 8 KB total for all headers. */
 #define MAX_HEADERS 100            /** 100 headers maximum. */


 /**
  * Forward declarations for callback.
  */

  int zeus_event_ctl(zeus_server_t *server, zeus_io_event_t *ev, int op, uint32_t events);
  static void accept_connection_cb(zeus_io_event_t *ev);
  static void handle_read_cb(zeus_io_event_t *ev);
  static void parse_http_request(zeus_conn_t *conn);
  void close_connection(zeus_conn_t *conn);

  /**
   * Sets a file descriptor to non-blocking mode.
   */

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/**
 * Executes the blocking event loop for a single worker process.
 */

int zeus_worker_loop(zeus_server_t *server) {

    /**
     * Create epoll instance.
     */

    server->loop_fd = epoll_create1(0);
    if (server->loop_fd < 0) {
        ZLOG_PERROR("Worker fatal: epoll_create1 failed.");
        return -1;
    }

    /**
     * Register the inherited listen socket (FD is already valid and non-blocking).
     */

    zeus_io_event_t *listen_event = calloc(1, sizeof(*listen_event));

    if (!listen_event) {
        ZLOG_PERROR("calloc listen_event failed");
        close(server->loop_fd);
        return -1;
    }
    
    listen_event->fd = server->listen_fd;
    listen_event->data = server;
    listen_event->read_cb = accept_connection_cb;
    listen_event->write_cb = NULL;

    if (zeus_event_ctl(server, listen_event, EPOLL_CTL_ADD, EPOLLIN | EPOLLET) == -1) {
        ZLOG_PERROR("Worker fatal: epoll_ctl listen_fd failed");
        free(listen_event);
        close(server->loop_fd);
        return -1;
    }

    ZLOG_INFO("Worker (PID %d) is ready to accept connections.\n", getpid());

    /**
     * Blocking I/O loop.
     */

    struct epoll_event *events = calloc(ZEUS_MAX_EVENTS, sizeof(*events));

    if (!events) {
        ZLOG_PERROR("calloc events failed");
        free(listen_event);
        close(server->loop_fd);
        return -1;
    }

    while (!shutdown_requested) {
        int n_fds = epoll_wait(server->loop_fd, events, ZEUS_MAX_EVENTS, -1);
        if (n_fds < 0) {
            if (errno == EINTR) {
                if (shutdown_requested) {
                    break;
                }
                continue;
            }
            ZLOG_PERROR("epoll_wait fatal error");
            break;  /** Break outer while, then clean up below */
        }

        for (int i = 0; i < n_fds; i++) {
            zeus_io_event_t *ev = events[i].data.ptr;

            if (!ev) {

                /** 
                 * Defensive logging 
                 */

                fprintf(stderr, "Worker (PID %d) got NULL event.ptr\n", getpid());
                continue;
            }

            if ((events[i].events & EPOLLIN) && ev->read_cb) {
                ev->read_cb(ev);
            }

            if ((events[i].events & EPOLLOUT) && ev->write_cb) {
                ev->write_cb(ev);
            }

            if (shutdown_requested) {
                break;
            }
        }
    }

    free(events);

    /**
     * Cleanup on exit from loop 
     */
    
    if (server->loop_fd >= 0) {
        close(server->loop_fd);
        server->loop_fd = -1;
    }

    return 0;
}

/**
 * Adds or modifies an FD in the epoll instance.
 */
int zeus_event_ctl(zeus_server_t *server, zeus_io_event_t *ev, int op, uint32_t events) {
#ifdef __linux__
    struct epoll_event event;
    event.events = events;
    event.data.ptr = ev;
    
    return epoll_ctl(server->loop_fd, op, ev->fd, &event);
#else
    /**
     * TODO: implement kqueue logic here.
     */
    return -1;
#endif 
}

/**
 * Callback when the listen socket is ready for reading (new connection).
 */

static void accept_connection_cb(zeus_io_event_t *ev) {
    zeus_server_t *server = (zeus_server_t *)ev->data;
    struct sockaddr_in client_addr;
    socklen_t addr_len;
    int conn_fd;

    /**
     * Loop to accept all pending connections (edge-triggered behavior safety)
     */

    while (1) {
        addr_len = sizeof(client_addr); /* reset for each accept() */
        conn_fd = accept(server->listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (conn_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                ZLOG_PERROR("Accept error");
                break;
            }
        }

        if (set_nonblocking(conn_fd) == -1) {
            close(conn_fd);
            continue;
        }

        /**
         * Allocate and initialize the connection structure.
         */

        zeus_conn_t *conn = calloc(1, sizeof(zeus_conn_t));
        if (!conn) {
            close(conn_fd);
            continue;
        }

        conn->server = server;
        conn->event.fd = conn_fd;
        conn->event.data = conn;
        conn->event.read_cb = handle_read_cb;
        conn->event.write_cb = handle_write_cb;

        /**
         * Initialize the SSL object and handshake.
         */

        conn->ssl_conn = SSL_new(server->ssl_ctx);
        if (!conn->ssl_conn) {
            ZLOG_PERROR("SSL_new failed");
            close_connection(conn);
            continue;
        }

        SSL_set_fd(conn->ssl_conn, conn_fd);
        SSL_set_accept_state(conn->ssl_conn);

        conn->is_ssl = 1;
        conn->handshake_done = 0;

        if (zeus_event_ctl(server, &conn->event, EPOLL_CTL_ADD, EPOLLIN | EPOLLET) == -1) {
            ZLOG_PERROR("epoll_ctl new conn failed.");
            close_connection(conn);     /** safe cleanup */
            continue;
        }

        ZLOG_INFO("New connection accepted: FD %d (handshake started).\n", conn_fd);
    }

    if (conn_fd == -1 && errno != EWOULDBLOCK && errno != EAGAIN) {
        ZLOG_PERROR("Accept error");
    }
}

/**
 * Callback when a client socket is ready for reading (data available).
 * It handles the TLS handshake continuation and the subsequent encrypted/plaintext
 * reading.
 */

static void handle_read_cb(zeus_io_event_t *ev) {
    zeus_conn_t *conn = (zeus_conn_t *)ev->data;

    /**
     * Manage TLS handshake continuation.
     */

    if (conn->is_ssl && !conn->handshake_done) {
        int hs_result = zeus_handle_ssl_handshake(conn);
        if (hs_result < 0) {
            return;         /** Handshake failed. Connection closed internally. */
        }

        if (hs_result == 0) {
            return;         /** Waiting for the next event. */
        }
    }

    /**
     * Determine the read function based on connection type.
     */

    ssize_t (*read_func)(int, void*, size_t) = read;
    if (conn->is_ssl && conn->handshake_done) {
        read_func = (ssize_t (*)(int, void*, size_t))SSL_read;
    }
    ssize_t bytes_received = 0;
    int error = 0;

    while (conn->buffer_used < sizeof(conn->read_buffer) - 1) {
        size_t space_available = sizeof(conn->read_buffer) - conn->buffer_used - 1;

        if (conn->is_ssl && conn->handshake_done) {
            /**
             * Read encrypted data.
             */
            bytes_received = SSL_read(conn->ssl_conn, conn->read_buffer + conn->buffer_used, space_available);
        } else {
            /**
             * Read plaintext data.
             */
             bytes_received = read(conn->event.fd, conn->read_buffer + conn->buffer_used, space_available);
        }

        if (bytes_received > 0) {
            conn->buffer_used += (size_t)bytes_received;
            conn->read_buffer[conn->buffer_used] = '\0';

            /**
             * Pass the control to the state machine.
             */

             http_parser_run(conn);

             /**
              * If parsing finished or errored, stop reading.
              */

            if (conn->parser_state >= PS_COMPLETED || conn->parser_state == PS_ERROR) {
                return;
            }
        } else if (bytes_received == 0) {
            /**
             * Graceful shutdown (closed by client).
             */

            ZLOG_INFO("Connection closed by client: FD %d\n", conn->event.fd);
            close_connection(conn);
            return;
        } else {
            if (conn->is_ssl && conn->handshake_done) {
                error = SSL_get_error(conn->ssl_conn, bytes_received);
                if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
                    return;     /** Socket exhaustion or need writing. */
                }
                ERR_print_errors_fp(stderr);
            } else {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    return;     /** Socket exhausted :p */
                }
                ZLOG_PERROR("read error");
            }
            
            close_connection(conn);
            return;
        }
    }

    /**
     * Just a security check.
     */

     if (conn->buffer_used >= sizeof(conn->read_buffer) - 1) {
        ZLOG_INFO("Security: Buffer limit reached on FD %d\n", conn->event.fd);
        close_connection(conn);
    }
}




/**
 * The core HTTP State Machine. Parses the request incrementally.
 */
static void parse_http_request(zeus_conn_t *conn) {
    char *buffer = conn->read_buffer;
    size_t len = conn->buffer_used;
    char *end_of_headers = NULL;

    /**
     * Look for "\r\n\r\n" which signifiers the end of headers.
     */

    end_of_headers = strstr(buffer, "\r\n\r\n");

    if (conn->parser_state <= PS_HEADERS_FINISHED) {
        if (!end_of_headers) {
            /**
             * Check security limit early: if we read MAX_HEADER_LEN bytes and haven't
             * found the end of headers, it's an attack of malformed request.
             */
            if (len > MAX_HEADERS_LEN) {
                ZLOG_INFO("Security Limit: Headers too long.\n");
                conn->parser_state = PS_ERROR;
                /**
                 * TODO: 431 response.
                 */
                return;
            }
            /**
             * Need more data.
             */
            conn->parser_state = PS_HEADERS;
            return;
        }

        /**
         * Headers are finished. Perform actual parsing logic here.
         * 1 - Parse Start Line (Method, Path, Version).
         * 2 - Parse All Headers
         * 3 - Set conn->req->method and conn->req->path
         */
        conn->parser_state = PS_HEADERS_FINISHED;
        ZLOG_INFO("HTTP headers finished processing.\n");

        /**
         * After processing headers, determine body state (Content-Lenght vs Chunked).
         * For simplicity we assume PS_COMPLETED immediately after headers for now.
         */

        conn->parser_state = PS_COMPLETED;
    }

    if (conn->parser_state == PS_COMPLETED) {
        ZLOG_INFO("Request fully parsed. Dispatching handler.\n");
    }

    /**
     * TODO: Map path to handler function and call it.
     * For now, call a placeholder handler function.
     * The real request/response data should be correctly populated
     * first.
     */

    router_dispatch(conn);

    close_connection(conn);
}


/**
 * Cleans up resources and closes the connection.
 */

void close_connection(zeus_conn_t *conn) {
    if (!conn) return;

    int fd = conn->event.fd;
    /* remove from epoll */
#ifdef __linux__
    zeus_event_ctl(conn->server, &conn->event, EPOLL_CTL_DEL, 0);
#endif

    if (conn->ssl_conn) {
        /** 
         * Attempt non-blocking TLS close_notify exchange 
         */

        int r = SSL_shutdown(conn->ssl_conn);
        if (r == 0) {

            /**
             * peer did not respond yet.
             * Send FIN to avoid RST.
             */

            shutdown(fd, SHUT_RDWR);
            SSL_shutdown(conn->ssl_conn);
        }
        SSL_free(conn->ssl_conn);
        conn->ssl_conn = NULL;
    } else {
        shutdown(fd, SHUT_RDWR);
    }

    if (fd >= 0) {
        close(fd);
    }

    fprintf(stderr, "Connection FD %d closed.\n", fd);
    free(conn);
}

 /**
  * Initializes the server, creates epoll instance, and binds the socket.
  */

 zeus_server_t* zeus_server_init(zeus_config_t *config) {
    const char *host = config->bind_host;
    const uint16_t port = config->bind_port;

    zeus_server_t *server = calloc(1, sizeof(zeus_server_t));
    if (!server) {
        return NULL;
    }

    server->config = *config;

    /**
     * Create Socket (using SOCK_NONBLOCK for asynchronous I/O)
     */

    server->listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server->listen_fd < 0) {
        ZLOG_PERROR("socket failed");
        free(server);
        return NULL;
    }

    /**
     * Set Socket Options (SO_REUSEADDR)
     */

    int opt = 1;
    if (setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ZLOG_PERROR("setsockopt SO_REUSEADDR failed");
        close(server->listen_fd);
        free(server);
        return NULL;
    }

    /**
     * Bind Address
     */

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        ZLOG_PERROR("inet_pton failed (invalid host address)");
        close(server->listen_fd);
        free(server);
        return NULL;
    }

    if (bind(server->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ZLOG_PERROR("bind failed");
        close(server->listen_fd);
        free(server);
        return NULL;
    }

    /**
     * Listen (Start accepting connections)
     */

    if (listen(server->listen_fd, 4096) < 0) {
        ZLOG_PERROR("listen failed");
        close(server->listen_fd);
        free(server);
        return NULL;
    }

    /**
     * Drop Privileges (Security check, after listen)
     */

    if (geteuid() == 0 && zeus_drop_privileges() < 0) {
        ZLOG_FATAL("Fatal: Cannot drop privileges. Aborting.\n");
        return NULL;
    }
    
    /**
     * Register the listen socket's event
     */
    
    static zeus_io_event_t listen_event;
    listen_event.fd = server->listen_fd;
    listen_event.data = server;
    listen_event.read_cb = accept_connection_cb;

    /** 

    if (zeus_event_ctl(server, &listen_event, EPOLL_CTL_ADD, EPOLLIN | EPOLLET) == -1) {
        ZLOG_PERROR("epoll_ctl listen_fd failed");
        return NULL;
    }
    */

    ZLOG_INFO("listen_fd = %d\n", server->listen_fd);
    ZLOG_INFO("zeusHttp running on %s:%d (FD: %d)\n", host, port, server->listen_fd);
    ZLOG_INFO("Security: Privileges successfully dropped.\n"); 
    
    return server;
}

 /**
  * Starts the main I/O loop using epoll_wait.
  */

int zeus_server_run(zeus_server_t *server) {
#ifdef __linux__
    struct epoll_event events[ZEUS_MAX_EVENTS];

    while (1) {
        int n_fds = epoll_wait(server->loop_fd, events, ZEUS_MAX_EVENTS, -1);
        if (n_fds == -1) {
            if (errno == EINTR) continue;
            ZLOG_PERROR("epoll_wait failed");
            return -1;
        }

        for (int i = 0; i < n_fds; i++) {
            zeus_io_event_t *ev = events[i].data.ptr;

            /** 
             * READ EVENTS 
             */

            if (events[i].events & EPOLLIN) {
                if (ev->read_cb)
                    ev->read_cb(ev);
            }

            /** 
             * WRITE EVENTS 
             */

            if (events[i].events & EPOLLOUT) {

                if (!ev->write_cb)
                    continue;

                zeus_conn_t *c = NULL;

                if (ev->fd != server->listen_fd)
                    c = (zeus_conn_t *)ev->data;

                if (c) {

                    if (c->is_ssl && !c->handshake_done) {
                        continue;
                    }

                    /* ok to write */
                    ev->write_cb(ev);
                } else {
                    ev->write_cb(ev);
                }
            }
        }
    }

    return 0;

#else
    fprintf(stderr, "Event loop not implemented for this OS.\n");
    return -1;
#endif
}

/**
 * Register a handler (placeholder).
 */
int zeus_server_add_handler(zeus_server_t *server, const char *path, zeus_handler_cb handler) {
    return router_add_handler(server, path, handler);
}


/**
 * Sends a file using zero-copy (placeholder).
 *
int zeus_response_send_file(zeus_response_t *res, const char *filepath) {
    return 0;
}
*/





