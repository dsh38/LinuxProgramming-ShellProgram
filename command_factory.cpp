// command_factory.cpp - simple factory creating Command objects
#include "command_factory.h"
#include "command.h"
#include "builtin_registry.h"
#include "builtin_command.h"

std::unique_ptr<Command> CommandFactory::createFromLines(const std::vector<CommandLine>& lines) const {
    if (lines.empty()) return nullptr;
    if (lines.size() == 1) {
        // single stage: check builtin registry first
        const CommandLine &cl = lines[0];
        if (!cl.argv.empty()) {
            auto fn = BuiltinRegistry::instance().lookup(cl.argv[0]);
            if (fn) return std::make_unique<BuiltinCommand>(fn, cl);
        }
        return std::make_unique<SimpleCommand>(cl);
    }
    // pipeline: create PipelineCommand; builtins inside pipelines are not run in-process
    std::vector<CommandLine> stages = lines;
    return std::make_unique<PipelineCommand>(std::move(stages));
}
