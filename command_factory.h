// command_factory.h - create Command objects from parsed CommandLine(s)
#ifndef TEAMSHELL_COMMAND_FACTORY_H
#define TEAMSHELL_COMMAND_FACTORY_H

#include "command.h"
#include "parser.h"
#include <memory>
#include <vector>

class CommandFactory {
public:
    // create a Command (SimpleCommand or PipelineCommand) from parsed CommandLine(s)
    std::unique_ptr<Command> createFromLines(const std::vector<CommandLine>& lines) const;
};

#endif // TEAMSHELL_COMMAND_FACTORY_H
