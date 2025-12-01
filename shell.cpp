// shell.cpp - minimal Shell implementation replacing system() calls
#include "shell.h"
#include "parser.h"
#include "command_factory.h"
#include <vector>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <glob.h>
#include <cstring>
#include <iostream>
// readline for interactive prompt
#include <readline/readline.h>
#include <readline/history.h>
#include <signal.h>
#include "runtime_state.h"

// forward declare signal handlers so constructor can register them
static void sigint_handler(int);
static void sigtstp_handler(int);
static void sigquit_handler(int);

Shell::Shell() {
    // register signal handlers to forward to foreground process group
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);
    signal(SIGQUIT, sigquit_handler);
}

int Shell::runNonInteractive(std::istream &in) {
    // If stdin is a TTY, offer an interactive prompt using readline.
    if (isatty(STDIN_FILENO)) {
        // initialize readline completion to filename completion
        rl_attempted_completion_function = (rl_completion_func_t *) (void *) rl_filename_completion_function;
        while (true) {
            char cwd[4096] = "";
            if (!getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
            // Prompt format: teamshell:<cwd>
            std::string prompt = std::string("teamshell:") + cwd + ">";
            char *line = readline(prompt.c_str());
            if (!line) break; // EOF (Ctrl-D)
            if (line[0] != '\0') {
                add_history(line);
                handleLine(std::string(line));
            }
            free(line);
        }
        return 0;
    }

    std::string line;
    while (std::getline(in, line)) {
        handleLine(line);
    }
    return 0;
}

// signal handlers forward to fg_pgid defined in runtime_state.cpp
static void sigint_handler(int sig) {
    (void)sig;
    if (fg_pgid != 0) {
        kill(- (pid_t)fg_pgid, SIGINT);
    }
}

static void sigtstp_handler(int sig) {
    (void)sig;
    if (fg_pgid != 0) {
        kill(- (pid_t)fg_pgid, SIGTSTP);
    }
}

static void sigquit_handler(int sig) {
    (void)sig;
    if (fg_pgid != 0) {
        kill(- (pid_t)fg_pgid, SIGQUIT);
    }
}

// execute_pipeline removed: command execution is handled by Command objects

void Shell::handleLine(const std::string &line) {
    auto stage_strs = parser_.splitPipeline(line);
    if (stage_strs.empty()) return;
    std::vector<CommandLine> cmds;
    for (const auto &s: stage_strs) cmds.push_back(parser_.parse(s));

    // Parent-side globbing: expand wildcard args before execution
    for (auto &cl : cmds) {
        std::vector<std::string> newargv;
        for (const auto &a : cl.argv) {
            bool has_wild = (a.find_first_of("*?[") != std::string::npos);
            if (!has_wild) { newargv.push_back(a); continue; }
            glob_t g; memset(&g, 0, sizeof(g));
            int ret = glob(a.c_str(), 0, NULL, &g);
            if (ret == 0) {
                for (size_t i = 0; i < g.gl_pathc; ++i) newargv.emplace_back(g.gl_pathv[i]);
                globfree(&g);
            } else {
                // no matches -> keep literal
                newargv.push_back(a);
            }
        }
        cl.argv.swap(newargv);
    }

        // Determine if any stage requested background execution
        bool background = false;
        for (const auto &cl : cmds) if (cl.background) background = true;

        // If single stage and builtin that should run in parent
    if (cmds.size() == 1 && !cmds[0].argv.empty()) {
        const auto &argv = cmds[0].argv;
        if (argv[0] == "exit") exit(0);
        if (argv[0] == "cd") {
            const char *path = nullptr;
            if (argv.size() >= 2) path = argv[1].c_str();
            else path = getenv("HOME");
            if (!path) path = "/";
            if (chdir(path) != 0) perror("chdir");
            return;
        }
        if (argv[0] == "pwd") {
            char buf[4096]; if (getcwd(buf, sizeof(buf))) puts(buf); else perror("getcwd");
            return;
        }
        if (argv[0] == "ls") {
            // call C++ builtin ls
            extern int ls_builtin(const CommandLine &cl);
            ls_builtin(cmds[0]);
            return;
        }
        if (argv[0] == "grep") {
            extern int grep_builtin(const CommandLine &cl);
            grep_builtin(cmds[0]);
            return;
        }
        if (argv[0] == "cp") {
            extern int cp_builtin(const CommandLine &cl);
            cp_builtin(cmds[0]);
            return;
        }
        if (argv[0] == "mv") {
            extern int mv_builtin(const CommandLine &cl);
            mv_builtin(cmds[0]);
            return;
        }
        if (argv[0] == "rm") {
            extern int rm_builtin(const CommandLine &cl);
            rm_builtin(cmds[0]);
            return;
        }
        if (argv[0] == "ln") {
            extern int ln_builtin(const CommandLine &cl);
            ln_builtin(cmds[0]);
            return;
        }
        if (argv[0] == "mkdir") {
            extern int mkdir_builtin(const CommandLine &cl);
            mkdir_builtin(cmds[0]);
            return;
        }
        if (argv[0] == "rmdir") {
            extern int rmdir_builtin(const CommandLine &cl);
            rmdir_builtin(cmds[0]);
            return;
        }
        if (argv[0] == "cat") {
            extern int cat_builtin(const CommandLine &cl);
            cat_builtin(cmds[0]);
            return;
        }
    }

    // otherwise execute pipeline (may be single-stage non-builtin)
    CommandFactory factory;
    auto cmd = factory.createFromLines(cmds);
    if (cmd) cmd->execute(background);
}
