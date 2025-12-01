// builtin_command.h - wrapper Command that invokes a builtin function
#ifndef TEAMSHELL_BUILTIN_COMMAND_H
#define TEAMSHELL_BUILTIN_COMMAND_H

#include "command.h"
#include "builtin_registry.h"

class BuiltinCommand : public Command {
public:
    BuiltinCommand(builtin_fn fn, const CommandLine &cl) : fn_(fn), cl_(cl) {}
    int execute(bool background) override {
        (void)background; // builtins run in-process
        if (fn_) return fn_(cl_);
        return 127;
    }
private:
    builtin_fn fn_;
    CommandLine cl_;
};

#endif // TEAMSHELL_BUILTIN_COMMAND_H
