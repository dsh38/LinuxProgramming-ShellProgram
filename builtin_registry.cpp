#include "builtin_registry.h"
#include <mutex>

BuiltinRegistry &BuiltinRegistry::instance() {
    static BuiltinRegistry inst;
    return inst;
}

void BuiltinRegistry::registerBuiltin(const std::string &name, builtin_fn fn) {
    map_[name] = fn;
}

builtin_fn BuiltinRegistry::lookup(const std::string &name) const {
    auto it = map_.find(name);
    if (it == map_.end()) return nullptr;
    return it->second;
}
