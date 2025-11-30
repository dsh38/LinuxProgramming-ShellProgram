// builtins.h - builtin command declarations
#ifndef TEAMSHELL_BUILTINS_H
#define TEAMSHELL_BUILTINS_H

#include "parser.h"

// Return 0 to indicate shell should exit, non-zero to continue
int ls_builtin(const CommandLine &cl);
int grep_builtin(const CommandLine &cl);
int cp_builtin(const CommandLine &cl);
int mv_builtin(const CommandLine &cl);
int rm_builtin(const CommandLine &cl);
int ln_builtin(const CommandLine &cl);
int mkdir_builtin(const CommandLine &cl);
int rmdir_builtin(const CommandLine &cl);
int cat_builtin(const CommandLine &cl);

#endif // TEAMSHELL_BUILTINS_H
