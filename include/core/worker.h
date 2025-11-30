/**
 * include/core/worker.h
 * Defines worker management structures and the main worker loop.
 */

#ifndef ZEUS_WORKER_H
#define ZEUS_WORKER_H

#include "../zeushttp.h"
#include <sys/types.h>

/**
 * Status codes for the worker process.
 */

typedef enum {
    WORKER_STATUS_IDLE,
    WORKER_STATUS_RUNNING,
    WORKER_STATUS_EXITING
} worker_status_t;

/**
 * Structure to hold details about a running worker.
 */

typedef struct zeus_worker {
    pid_t pid;
    worker_status_t status;
    int core_id;    /** For CPU affinity. */
} zeus_worker_t;

/**
 * Starts the main master loop, which spawns and manages workers.
 */

int worker_master_start(zeus_server_t *server, int num_workers);

/**
 * Runs the event loop specific to a single worker process.
 */

int worker_process_run(zeus_server_t *server);

#endif  // ZEUS_WORKER_H