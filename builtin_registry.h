// builtin_registry.h - registry for builtin commands
#ifndef TEAMSHELL_BUILTIN_REGISTRY_H
#define TEAMSHELL_BUILTIN_REGISTRY_H

#include "parser.h"
#include <string>
#include <functional>
#include <unordered_map>

using builtin_fn = std::function<int(const CommandLine&)>;

class BuiltinRegistry {
public:
    static BuiltinRegistry &instance();
    void registerBuiltin(const std::string &name, builtin_fn fn);
    builtin_fn lookup(const std::string &name) const;
private:
    std::unordered_map<std::string, builtin_fn> map_;
};

#endif // TEAMSHELL_BUILTIN_REGISTRY_H
