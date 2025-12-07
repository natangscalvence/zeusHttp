#ifndef WORKER_SIGNALS_H
#define WORKER_SIGNALS_H

#include <signal.h>

/** Global flags triggered by SIGHUP */

extern volatile sig_atomic_t reload_requested;
extern volatile sig_atomic_t shutdown_requested;

#endif // WORKER_SIGNALS_H