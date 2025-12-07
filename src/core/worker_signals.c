#include "../../include/core/worker_signals.h"

volatile sig_atomic_t shutdown_requested = 0;
volatile sig_atomic_t reload_requested = 0;

