/**
 * worker.c
 * Implements the Master-Worker prefork model for resilience
 * and concurrency.
 */

#define _POSIX_C_SOURCE 200809L

#include "../../include/zeushttp.h"
#include "../../include/core/worker.h"
#include "../../include/core/conn.h" 
#include "../../include/core/server.h"
#include "../../include/core/worker_signals.h"
#include "../../include/core/log.h"
#include "../../include/config/config.h" 
#include <signal.h> 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>


extern int worker_main_loop(zeus_server_t *server);
extern int zeus_worker_loop(zeus_server_t *server);
extern int zeus_drop_privileges();

static void master_reload_workers(zeus_server_t *server);



/**
 * Global array to track workers 
 */
static zeus_worker_t *Workers = NULL;
static int Num_Workers = 0;

/**
 * Handler for SIGHUP.
 */

static void master_signal_handler(int signo) {
    switch (signo) {
        case SIGHUP:
            reload_requested = 1;
            break;
        case SIGQUIT:
        case SIGTERM:
            shutdown_requested = 1;
            break;
    }
}

/**
 * Spawns a single worker process.
 */

static pid_t worker_spawn(zeus_server_t *server, int worker_id) {
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("Error during fork.");
        return -1;
    }
    if (pid == 0) {
        ZLOG_INFO("Worker %d (PID %d) starting up.\n", worker_id, getpid());
        if (zeus_drop_privileges() < 0) {
            ZLOG_FATAL("Worker Fatal: Cannot drop privileges. Exiting.");
            exit(EXIT_FAILURE);
        }
        /**
         * Run the main event loop (blocking call).
         */

        int rc = worker_process_run(server);
        if (rc == 0) {
            ZLOG_INFO("Worker %d (PID %d) exiting normally.\n", worker_id, getpid());
            _exit(EXIT_SUCCESS);
        } else {
            ZLOG_FATAL("Worker %d (PID %d) exiting with error (rc=%d).", worker_id, getpid(), rc);
            _exit(EXIT_FAILURE);
        }
    }
    return pid;
}

/**
 * Initializes the signal handlers for the Master process.
 */

static void master_init_signals() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = master_signal_handler;
    

    /**
     * Register handlers for SIGUP for reload, SIGQUIT and SIGTERM for
     * shutdown.
     */

    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /**
     * Ignore SIGPIPE (to prevent master process from crashing on broken connections.)
     */

    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
}

/**
 * Initiates the graceful reload cycle
 * Spawns new workers (new configuration).
 */

static void master_reload_workers(zeus_server_t *server) {
    ZLOG_INFO("Master: Starting graceful reload cycle.\n");

    const int new_num_workers = server->config.num_workers;

    /**
     * Signal all currently running workers to stop accepting 
     * new connections.
     */

    for (int i = 0; i < Num_Workers; i++) {
        if (Workers[i].status == WORKER_STATUS_RUNNING) {
            ZLOG_INFO("Master: Signaling old worker %d (PID %d) to exit gracefully.\n", i, Workers[i].pid);

            kill(Workers[i].pid, SIGQUIT);
            Workers[i].status = WORKER_STATUS_EXITING;
        }
    }

    /**
     * Spawn new workers to immediately take over new connections.
     */

    for (int i = 0; i < Num_Workers; i++) {
        if (Workers[i].status != WORKER_STATUS_RUNNING) {
            Workers[i].pid = worker_spawn(server, i);

            if (Workers[i].pid > 0) {
                Workers[i].status = WORKER_STATUS_RUNNING;
                ZLOG_INFO("Master: New worker %d spawned (PID %d).\n", i, Workers[i].pid);
            }
        }
    }

    Num_Workers = new_num_workers;
}




/**
 * Main master loop for starting and monitoring workers.
 */

int worker_master_start(zeus_server_t *server) {
    Num_Workers = server->config.num_workers;

    Workers = calloc(server->config.num_workers, sizeof(zeus_worker_t));
    if (!Workers) {
        ZLOG_FATAL("Master: Cannot allocate workers array.");
        return -1;
    }

    master_init_signals();
    ZLOG_INFO("Master (PID %d) starting %d workers.\n", getpid(), server->config.num_workers);

    /**
     * Initial spawn of workers.
     */

    for (int i = 0; i < server->config.num_workers; i++) {
        Workers[i].pid = worker_spawn(server, i);
        if (Workers[i].pid > 0) {
            Workers[i].status = WORKER_STATUS_RUNNING;
        }
    }

    /**
     * Master monitoring loop (relisience and graceful reload).
     */

    while (!shutdown_requested) {
        int status;

        /**
         * Use WNOHANG to check for dead workers without blocking indefinitely.
         */

        pid_t dead_pid = waitpid(-1, &status, WNOHANG);

        if (dead_pid > 0) {
            ZLOG_INFO("Master: Worker (PID %d) died. Status: %d\n", dead_pid, status);

            for (int i = 0; i < Num_Workers; i++) {
                if (Workers[i].pid == dead_pid) {
                    if (Workers[i].status == WORKER_STATUS_EXITING) {
                        ZLOG_INFO("Master: Old worker %d finished gracefully.\n", i);
                    } else {
                        ZLOG_INFO("Master: Worker %d died unexpectedly. Restarting...\n", i);
                    }

                    /**
                     * Restart workers to maintain concurrency level.
                     */

                    Workers[i].pid = worker_spawn(server, i);
                    if (Workers[i].pid > 0) {
                        Workers[i].status = WORKER_STATUS_RUNNING;

                        printf("Master: Worker %d successfully restarted (new PID %d).\n", i, Workers[i].pid);
                    }
                    break;
                }
            }
        }

        /**
         * Check if reload was requested 
         */

        if (reload_requested) {
            master_reload_workers(server);
            reload_requested = 0;
        }

        usleep(100000);     /** prevent CPU spin */
    }

    /**
     * Graceful shutdown: Master sends termination signal to all running 
     * workers.
     */

    ZLOG_INFO("Master: Initiating final shutdown. Signaling all workers to terminate.\n");
    for (int i = 0; i < Num_Workers; i++) {
        if (Workers[i].pid > 0 && Workers[i].status == WORKER_STATUS_RUNNING) {
            kill(Workers[i].pid, SIGQUIT);
        }
    }

    /**
     * Wait for remaining workers to exit.
     */

    int workers_left = Num_Workers;
    while (1) {
        pid_t dead_pid = waitpid(-1, NULL, 0);

        if (dead_pid > 0) {
            workers_left--;
            ZLOG_INFO("Master: Confirmed termination of worker PID %d.\n", dead_pid);
        } else if (dead_pid == -1) {
            if (errno == ECHILD) {
                break;
            }
            if (errno != EINTR) {
                ZLOG_PERROR("waitpid during shutdown error.\n");
                break;
            }
        }
    }
    ZLOG_INFO("Master: All workers terminated. Exiting.\n");
    return 0;
}

/**
 * The main function executed by the worker process.
 */

int worker_process_run(zeus_server_t *server) {
    ZLOG_INFO("Worker (PID %d) entering event loop. Inherti listen_fd=%d", getpid(), server->listen_fd);
    

    if (server->listen_fd < 0) {
        ZLOG_PERROR("Worker fatal: inherited invalid listen_fd");
        return -1;
    }

    return zeus_worker_loop(server);
}