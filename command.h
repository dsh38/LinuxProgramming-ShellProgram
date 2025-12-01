// command.h - Command hierarchy for shell commands
#ifndef TEAMSHELL_COMMAND_H
#define TEAMSHELL_COMMAND_H

#include "parser.h"
#include <memory>
#include <vector>

class Command {
public:
    virtual ~Command() = default;
    // execute the command; background indicates whether parent should wait
    virtual int execute(bool background) = 0;
};

class SimpleCommand : public Command {
public:
    explicit SimpleCommand(const CommandLine &cl);
    int execute(bool background) override;
private:
    CommandLine cl_;
};

class PipelineCommand : public Command {
public:
    explicit PipelineCommand(std::vector<CommandLine> stages);
    int execute(bool background) override;
private:
    std::vector<CommandLine> stages_;
};

#endif // TEAMSHELL_COMMAND_H
