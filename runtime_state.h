// runtime_state.h - shared runtime state used across modules
#ifndef TEAMSHELL_RUNTIME_STATE_H
#define TEAMSHELL_RUNTIME_STATE_H

#include <signal.h>

extern volatile sig_atomic_t fg_pgid;

#endif // TEAMSHELL_RUNTIME_STATE_H
