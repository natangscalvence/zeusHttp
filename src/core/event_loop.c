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
int http_process_read_buffer(zeus_conn_t *conn);
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

static void zeus_dispatch_event(zeus_server_t *server, struct epoll_event *ee) {
    zeus_io_event_t *ev = (zeus_io_event_t *)ee->data.ptr;
    if (!ev) return;

    // Caso especial: Novo cliente tentando conectar
    if (ev->fd == server->listen_fd) {
        if ((ee->events & EPOLLIN) && ev->read_cb) {
            ev->read_cb(ev);
        }
        return;
    }

    // Caso Geral: Eventos em conexões existentes
    zeus_conn_t *conn = (zeus_conn_t *)ev->data;
    if (!conn) return;

    // PROTEÇÃO: Mantém a conexão viva durante a execução dos callbacks
    conn_ref(conn);

    /* Tratamento de LEITURA */
    if ((ee->events & EPOLLIN) && ev->read_cb && !conn->closing) {
        ev->read_cb(ev);
    }

    /* Tratamento de ESCRITA 
     * Verificamos closing novamente, pois o read_cb anterior pode ter fechado a conexão */
    if (!conn->closing && (ee->events & EPOLLOUT) && ev->write_cb) {
        ev->write_cb(ev);
        // Para SSL, garantimos que o handshake terminou antes de disparar write_cb de aplicação
        if (!conn->is_ssl || (conn->is_ssl && conn->handshake_done)) {
            ev->write_cb(ev);
        }
    }

    // FINALIZAÇÃO: Libera a referência deste ciclo. Se o refcount atingir 0, o free(conn) ocorre aqui.
    conn_unref(conn);
}

/**
 * @brief Loop principal do Worker Process.
 */
int zeus_worker_loop(zeus_server_t *server) {
    struct epoll_event *events = NULL;
    zeus_io_event_t *listen_ev = NULL;

    server->loop_fd = epoll_create1(0);
    if (server->loop_fd < 0) {
        ZLOG_PERROR("Worker fatal: epoll_create1 failed");
        return -1;
    }

    // Configuração do evento de escuta herdado
    listen_ev = calloc(1, sizeof(*listen_ev));
    if (!listen_ev) goto fatal;

    listen_ev->fd      = server->listen_fd;
    listen_ev->data    = server;
    listen_ev->read_cb = accept_connection_cb;

    if (zeus_event_ctl(server, listen_ev, EPOLL_CTL_ADD, EPOLLIN | EPOLLET) == -1) {
        ZLOG_PERROR("Worker fatal: epoll_ctl listen_fd failed");
        goto fatal;
    }

    events = calloc(ZEUS_MAX_EVENTS, sizeof(struct epoll_event));
    if (!events) goto fatal;

    ZLOG_INFO("Worker (PID %d) ready. listen_fd=%d", getpid(), server->listen_fd);

    while (!shutdown_requested) {
        int n_fds = epoll_wait(server->loop_fd, events, ZEUS_MAX_EVENTS, -1);
        
        if (n_fds < 0) {
            if (errno == EINTR) continue;
            ZLOG_PERROR("epoll_wait fatal error");
            break;
        }

        for (int i = 0; i < n_fds; i++) {
            zeus_dispatch_event(server, &events[i]);
            if (shutdown_requested) break;
        }
    }

    // Cleanup
    free(events);
    free(listen_ev);
    if (server->loop_fd >= 0) close(server->loop_fd);
    return 0;

fatal:
    if (listen_ev) free(listen_ev);
    if (events) free(events);
    if (server->loop_fd >= 0) close(server->loop_fd);
    return -1;
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
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int conn_fd = accept(server->listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        
        if (conn_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            ZLOG_PERROR("Accept error");
            break;
        }

        if (set_nonblocking(conn_fd) == -1) {
            close(conn_fd);
            continue;
        }

        zeus_conn_t *conn = calloc(1, sizeof(zeus_conn_t));
        if (!conn) {
            close(conn_fd);
            continue;
        }

        // INICIALIZAÇÃO DO REFCOUNT
        conn->refcount = 1; 
        conn->server = server;
        conn->event.fd = conn_fd;
        conn->event.data = conn;
        conn->event.read_cb = handle_read_cb;
        conn->event.write_cb = handle_write_cb;

        conn->ssl_conn = SSL_new(server->ssl_ctx);
        if (!conn->ssl_conn) {
            close_connection(conn);
            continue;
        }

        SSL_set_fd(conn->ssl_conn, conn_fd);
        SSL_set_accept_state(conn->ssl_conn);
        conn->is_ssl = 1;

        if (zeus_event_ctl(server, &conn->event, EPOLL_CTL_ADD, EPOLLIN | EPOLLET) == -1) {
            close_connection(conn);
            continue;
        }
        ZLOG_INFO("New connection: FD %d", conn_fd);
    }
}

/**
 * Callback when a client socket is ready for reading (data available).
 * It handles the TLS handshake continuation and the subsequent encrypted/plaintext
 * reading.
 */

static void handle_read_cb(zeus_io_event_t *ev) {
    zeus_conn_t *conn = ev->data;
    int should_close = 0;

    conn_ref(conn);

    if (conn->closing) {
        goto out;
    }

    /**
     * TLS handshake continuation
     */
    if (conn->is_ssl && !conn->handshake_done) {
        int hs = zeus_handle_ssl_handshake(conn);

        if (hs < 0) {
            should_close = 1;
            goto out;
        }

        if (hs == 0) {
            goto out; /* handshake incomplete */
        }
    }

    /**
     * Read loop (edge-triggered safe)
     */
    while (conn->buffer_used < sizeof(conn->read_buffer) - 1) {
        size_t space = sizeof(conn->read_buffer) - conn->buffer_used - 1;
        ssize_t n;

        if (conn->is_ssl && conn->handshake_done) {
            n = SSL_read(conn->ssl_conn,
                         conn->read_buffer + conn->buffer_used,
                         space);
        } else {
            n = read(conn->event.fd,
                     conn->read_buffer + conn->buffer_used,
                     space);
        }

        if (n > 0) {
            conn->buffer_used += (size_t)n;
            conn->read_buffer[conn->buffer_used] = '\0';

            http_parser_run(conn);

            if (conn->parser_state == PS_ERROR) {
                should_close = 1;
                break;
            }

            continue;
        }

        if (n == 0) {
            /* Client closed connection */
            should_close = 1;
            break;
        }

        /* n < 0 */
        if (conn->is_ssl && conn->handshake_done) {
            int ssl_err = SSL_get_error(conn->ssl_conn, n);
            if (ssl_err == SSL_ERROR_WANT_READ ||
                ssl_err == SSL_ERROR_WANT_WRITE) {
                break;
            }
            ERR_print_errors_fp(stderr);
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            ZLOG_PERROR("read error");
        }

        should_close = 1;
        break;
    }

    /**
     * Security: buffer overflow protection
     */
    if (conn->buffer_used >= sizeof(conn->read_buffer) - 1) {
        ZLOG_WARN("Security: read buffer limit reached (fd=%d)",
                  conn->event.fd);
        should_close = 1;
    }

out:
    if (should_close) {
        close_connection(conn);
    }

    conn_unref(conn);
}



/**
 * The core HTTP State Machine. Parses the request incrementally.
 */

int http_process_read_buffer(zeus_conn_t *conn) {
    char *buffer = conn->read_buffer;
    size_t len = conn->buffer_used;
    char *end_of_headers = NULL;

    /**
     * Lookup for header terminator.
     */
    
    end_of_headers = strstr(buffer, "\r\n\r\n");

    /**
     * Waiting for headers.
     */

    if (conn->parser_state <= PS_HEADERS_FINISHED) {
        if (!end_of_headers) {
            
            if (len > MAX_HEADERS_LEN) {            /** Security limits... */
                ZLOG_INFO("Security Limit: Headers too long. FD %d", conn->event.fd);
                conn->parser_state = PS_ERROR;
                close_connection(conn); 
                return -1;
            }
            
            conn->parser_state = PS_HEADERS;
            return 0; 
        }
        
        /** 
         * Calls parser from initia line to fill up conn->req.
         */

        if (parse_http_request(conn, &conn->req) < 0) {
            ZLOG_WARN("HTTP Parse: Failed to parse request line or headers. FD %d", conn->event.fd);
            conn->parser_state = PS_ERROR;
            close_connection(conn);
            return -1;
        }

        ZLOG_INFO("HTTP headers finished processing. FD %d", conn->event.fd);

        /**
         * Define the state to completed. (Ready for body but for while - COMPLETED).
         */

        conn->parser_state = PS_COMPLETED;
    }

    if (conn->parser_state == PS_COMPLETED) {
        ZLOG_INFO("Request fully parsed. Dispatching handler. FD %d", conn->event.fd);
        router_dispatch(conn); 
        return 0;
    }
    
    return 0; 
}

/**
 * Macros for reference counting.
 * This is the best way that i found to prevent UAF.
 */

inline void conn_ref(zeus_conn_t *c) {
    __atomic_add_fetch(&c->refcount, 1, __ATOMIC_SEQ_CST);
}

inline void conn_unref(zeus_conn_t *c) {
    if (!c) {
        return;
    }

    int refs = __atomic_sub_fetch(&c->refcount, 1, __ATOMIC_SEQ_CST);

    if (refs == 0) {
       free(c);
    }
}

/**
 * Cleans up resources and closes the connection.
 */

void close_connection(zeus_conn_t *conn) {
    if (!conn) {
        return;
    }

    if (__atomic_exchange_n(&conn->closing, 1, __ATOMIC_SEQ_CST)) {
        return;
    }

#ifdef __linux__
    zeus_event_ctl(conn->server, &conn->event, EPOLL_CTL_DEL, 0);
#endif

    conn->event.data = NULL;

    if (conn->ssl_conn) {
        SSL_shutdown(conn->ssl_conn);
        SSL_free(conn->ssl_conn);
        conn->ssl_conn = NULL;
    }

    if (conn->event.fd >= 0) {
        shutdown(conn->event.fd, SHUT_RDWR);
        close(conn->event.fd);
        conn->event.fd = -1;
    }
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





