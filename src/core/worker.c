/**
 * worker.c
 * Implements the Master-Worker prefork model for resilience
 * and concurrency.
 */

#include "../../include/zeushttp.h"
#include "../../include/core/worker.h"
#include "../../include/core/conn.h" 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

extern int worker_main_loop(zeus_server_t *server);
extern int zeus_worker_loop(zeus_server_t *server);
extern int zeus_drop_privileges();

/**
 * Global array to track workers 
 */
static zeus_worker_t *Workers = NULL;
static int Num_Workers = 0;

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
        printf("Worker %d (PID %d) starting up.\n", worker_id, getpid());
        if (zeus_drop_privileges() < 0) {
            fprintf(stderr, "Worker Fatal: Cannot drop privileges. Exiting.\n");
            exit(EXIT_FAILURE);
        }
        /**
         * Run the main event loop (blocking call).
         */

        int rc = worker_process_run(server);
        if (rc == 0) {
            printf("Worker %d (PID %d) exiting normally.\n", worker_id, getpid());
            _exit(EXIT_SUCCESS);
        } else {
            fprintf(stderr, "Worker %d (PID %d) exiting with error (rc=%d).\n", worker_id, getpid(), rc);
            _exit(EXIT_FAILURE);
        }
    }
    return pid;
}

/**
 * Master loop for spawning and monitoring workers.
 */
int worker_master_start(zeus_server_t *server, int num_workers) {
    Num_Workers = num_workers;
    Workers = calloc(num_workers, sizeof(zeus_worker_t));
    if (!Workers) {
        perror("calloc workers failed.");
        return -1;
    }
    printf("Master (PID %d) starting %d workers.\n", getpid(), num_workers);

    /**
     * Spawn initial workers.
     */
    for (int i = 0; i < num_workers; i++) {
        Workers[i].pid = worker_spawn(server, i);
        if (Workers[i].pid > 0) {
            Workers[i].status = WORKER_STATUS_RUNNING;
        }
    }

    /**
     * Master monitoring loop (resilience).
     */
    while (1) {
        int status;
        pid_t dead_pid = waitpid(-1, &status, 0);

        if (dead_pid > 0) {
            printf("Master: Worker (PID %d) died. Status: %d\n", dead_pid, status);

            /**
             * Restart worker.
             */
            for (int i = 0; i < num_workers; i++) {
                if (Workers[i].pid == dead_pid) {
                    Workers[i].pid = worker_spawn(server, i);
                    if (Workers[i].pid > 0) {
                        Workers[i].status = WORKER_STATUS_RUNNING;
                        printf("Master: Worker %d successfully restarted (New PID %d).\n", i, Workers[i].pid);
                    }
                    break;
                }
            }
        } else if (dead_pid == -1 && errno == ECHILD) {
            /**
             * No more children to wait for, exit if necessary.
             */
            break;
        } else if (dead_pid == -1 && errno != EINTR) {
            perror("waitpid error");
            break;
        }
    }
    return 0;
}

/**
 * The main function executed by the worker process.
 */
int worker_process_run(zeus_server_t *server) {
    printf("Worker (PID %d) entering event loop.\n", getpid());
    return zeus_worker_loop(server);
}