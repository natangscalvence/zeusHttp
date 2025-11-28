#include "../../include/zeushttp.h"
#include "../../include/http/http.h"
#include "../../include/core/conn.h"
#include "../../include/core/io_event.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#ifdef __linux__
#include <sys/epoll.h>
#define ZEUS_MAX_EVENTS 128
#define ZEUS_EVENT_LOOP_ID int
#endif

extern int zeus_drop_privileges();

/**
 * Security limits, adjustable.
 */

 #define MAX_HEADERS_LEN 8192       /** 8 KB total for all headers. */
 #define MAX_HEADERS 100            /** 100 headers maximum. */


/**
 * Main server structure definition (internal).
 */

 struct zeus_server {
    ZEUS_EVENT_LOOP_ID loop_fd;
    int listen_fd;
 };

 /**
  * Forward declarations for callback.
  */

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
 * Adds or modifies an FD in the epoll instance.
 */
static int zeus_event_ctl(zeus_server_t *server, zeus_io_event_t *ev, int op, uint32_t events) {
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
    socklen_t addr_len = sizeof(client_addr);
    int conn_fd;

    /**
     * Loop to accept all pending connections (edge-triggered behavior safety)
     */
    while ((conn_fd = accept(server->listen_fd, (struct sockaddr *)&client_addr, &addr_len)) > 0) {
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

        /**
         * Initialize connection and event.
         */
        conn->server = server;
        conn->event.fd = conn_fd;
        conn->event.data = conn;
        conn->event.read_cb = handle_read_cb;
        conn->event.write_cb = NULL;       /** Initially not write. */

        /**
         * Add to the event loop, listen for reads (incoming data).
         */
        if (zeus_event_ctl(server, &conn->event, EPOLL_CTL_ADD, EPOLLIN | EPOLLET) == -1) {
            perror("epoll_ctl new conn failed.");
            free(conn);
            close(conn_fd);

            continue;
        }
        printf("New connection accepted: FD %d\n", conn_fd);
    }
    if (conn_fd == -1 && errno != EWOULDBLOCK && errno != EAGAIN) {
        perror("Accept error");
    }
}

/**
 * Callback when a client socket is ready for reading (data available).
 */

static void handle_read_cb(zeus_io_event_t *ev) {
    zeus_conn_t *conn = (zeus_conn_t *)ev->data;

    ssize_t bytes_read = 0;
    while (conn->buffer_used < sizeof(conn->read_buffer) - 1) {
        /**
         * Read data directly into the connection's buffer, offset by
         * buffer_used.
         */
        bytes_read = read(
            conn->event.fd,
            conn->read_buffer + conn->buffer_used,
            sizeof(conn->read_buffer) - conn->buffer_used - 1
        );

        if (bytes_read > 0) {
            conn->buffer_used = (size_t)bytes_read;
            conn->read_buffer[conn->buffer_used] = '\0';

            /**
             * Pass the control to the state machine.
             */
            http_parser_run(conn);

            /**
             * If parsing finished or errored, stop reading.
             */
            if (conn->parser_state >= PS_COMPLETED) {
                break;
            }
        } else if (bytes_read == 0) {
            /**
             * Connection closed by client.
             */
            printf("Connection closed by client: FD %d\n", conn->event.fd);
            close_connection(conn);
            return;
        } else if (bytes_read == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                /**
                 * Done reading for now (socket exhausted).
                 */
                return;
            }
            /**
             * Real read error.
             */
            perror("read error");
            close_connection(conn);
            return;
        }
    }

    /**
     * Security check: Buffer overflow prevention.
     */
    if (conn->buffer_used >= sizeof(conn->read_buffer) - 1) {
        /**
         * Here we could send a 413 (Payload Too Large) or 431 (Header Fields Too Large).
         */
        printf("Security: Buffer limit reached on FD %d\n", conn->event.fd);
        /** TODO: send 431 response before closing. */
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
                printf("Security Limit: Headers too long.\n");
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
        printf("HTTP headers finished processing.\n");

        /**
         * After processing headers, determine body state (Content-Lenght vs Chunked).
         * For simplicity we assume PS_COMPLETED immediately after headers for now.
         */

        conn->parser_state = PS_COMPLETED;
    }

    if (conn->parser_state == PS_COMPLETED) {
        printf("Request fully parsed. Dispatching handler.\n");
    }

    /**
     * TODO: Map path to handler function and call it.
     * For now, call a placeholder handler function.
     * The real request/response data should be correctly populated
     * first.
     */

    /**
     * JUST A TEMPORARY DISPATCH AND RESPONSE.
     */
    zeus_request_t fake_req = { .method = "GET", .path = "/" };
    zeus_response_t fake_res = { .status_code = 200 };

    close_connection(conn);
}


/**
 * Cleans up resources and closes the connection.
 */

 void close_connection(zeus_conn_t *conn) {
    #ifdef __linux__
    zeus_event_ctl(conn->server, &conn->event, EPOLL_CTL_DEL, 0);
    #else 
     /** TODO: Implement kqueue deletion logic here. */
    #endif 

    close(conn->event.fd);

    free(conn);
    printf("Connection FD %d closed.\n", conn->event.fd);
 }

 /**
  * Initializes the server, creates epoll instance, and binds the socket.
  */

 zeus_server_t* zeus_server_init(const char *host, int port) {
    zeus_server_t *server = calloc(1, sizeof(zeus_server_t));
    if (!server) {
        return NULL;
    }

    /**
     * Create Socket (using SOCK_NONBLOCK for asynchronous I/O)
     */

    server->listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server->listen_fd < 0) {
        perror("socket failed");
        free(server);
        return NULL;
    }

    /**
     * Set Socket Options (SO_REUSEADDR)
     */

    int opt = 1;
    if (setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
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
        perror("inet_pton failed (invalid host address)");
        close(server->listen_fd);
        free(server);
        return NULL;
    }

    if (bind(server->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(server->listen_fd);
        free(server);
        return NULL;
    }

    /**
     * Listen (Start accepting connections)
     */

    if (listen(server->listen_fd, 4096) < 0) {
        perror("listen failed");
        close(server->listen_fd);
        free(server);
        return NULL;
    }

    /**
     * Drop Privileges (Security check, after listen)
     */

    if (geteuid() == 0 && zeus_drop_privileges() < 0) {
        fprintf(stderr, "Fatal: Cannot drop privileges. Aborting.\n");
        close(server->listen_fd);
        free(server);
        return NULL;
    }

    /**
     * Create EPOLL instance
     */

    server->loop_fd = epoll_create1(0);
    if (server->loop_fd < 0) {
        perror("epoll_create1 failed");
        close(server->listen_fd);
        free(server);
        return NULL;
    }
    
    /**
     * Register the listen socket's event
     */
    
    static zeus_io_event_t listen_event;
    listen_event.fd = server->listen_fd;
    listen_event.data = server;
    listen_event.read_cb = accept_connection_cb;

    if (zeus_event_ctl(server, &listen_event, EPOLL_CTL_ADD, EPOLLIN | EPOLLET) == -1) {
        perror("epoll_ctl listen_fd failed");
        close(server->loop_fd);
        close(server->listen_fd);
        free(server);
        return NULL;
    }

    printf("listen_fd = %d\n", server->listen_fd);
    printf("zeusHttp running on %s:%d (FD: %d)\n", host, port, server->listen_fd);
    printf("Security: Privileges successfully dropped.\n"); 
    
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
            if (errno == EINTR) {
                continue;
            }
            perror("epoll_wait failed");
            return -1;
        }
        for (int i = 0; i < n_fds; i++) {
            zeus_io_event_t *ev = events[i].data.ptr;

            if (events[i].events & EPOLLIN) {
                if (ev->read_cb) {
                    ev->read_cb(ev);
                }
            }

            if (events[i].events & EPOLLOUT) {
                if (ev->write_cb) {
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





