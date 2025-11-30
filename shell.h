// shell.h - Shell class declaration
#ifndef TEAMSHELL_SHELL_H
#define TEAMSHELL_SHELL_H

#include "parser.h"
#include <istream>

class Shell {
public:
    Shell();
    int runNonInteractive(std::istream &in);
    void handleLine(const std::string &line);
private:
    Parser parser_;
};

#endif // TEAMSHELL_SHELL_H
