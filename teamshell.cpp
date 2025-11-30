// teamshell.cpp - entrypoint that uses Shell class implemented in shell.*
#include "shell.h"
#include <iostream>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    Shell shell;
    return shell.runNonInteractive(std::cin);
}
