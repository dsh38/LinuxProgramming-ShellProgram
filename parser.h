// parser.h - quote-aware command parser declarations
#ifndef TEAMSHELL_PARSER_H
#define TEAMSHELL_PARSER_H

#include <string>
#include <vector>

struct CommandLine {
    std::vector<std::string> argv;
    bool background = false;
    std::string input_file;
    std::string output_file;
};

class Parser {
public:
    CommandLine parse(const std::string &cmd);
    std::vector<std::string> splitPipeline(const std::string &cmd);
};

#endif // TEAMSHELL_PARSER_H
