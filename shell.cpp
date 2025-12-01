// shell.cpp - minimal Shell implementation replacing system() calls
#include "shell.h"
#include "parser.h"
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

static void exec_cmd_argv(const CommandLine &cl) {
    // prepare argv
    if (cl.argv.empty()) _exit(0);
    std::vector<char*> cargs;
    for (const auto &s : cl.argv) cargs.push_back(const_cast<char*>(s.c_str()));
    cargs.push_back(nullptr);
    execvp(cargs[0], cargs.data());
    perror("execvp");
    _exit(127);
}

static void setup_redirections(const CommandLine &cl) {
    if (!cl.input_file.empty()) {
        int fd = open(cl.input_file.c_str(), O_RDONLY);
        if (fd < 0) { perror("open input"); _exit(127); }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    if (!cl.output_file.empty()) {
        int fd = open(cl.output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror("open output"); _exit(127); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
}

// foreground process group id (for signal forwarding)
static volatile sig_atomic_t fg_pgid = 0;

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

static void execute_pipeline(const std::vector<CommandLine> &cmds, bool background) {
    int n = cmds.size();
    int prev_fd = -1;
    std::vector<pid_t> pids;
    pid_t pgid = 0;
    for (int i = 0; i < n; ++i) {
        int pipefd[2] = {-1, -1};
        if (i < n-1) {
            if (pipe(pipefd) < 0) { perror("pipe"); return; }
        }
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return; }
        if (pid == 0) {
            // child
            // set process group for the pipeline
            if (pgid == 0) {
                setpgid(0, 0);
            } else {
                setpgid(0, pgid);
            }
            if (prev_fd != -1) {
                dup2(prev_fd, STDIN_FILENO);
            }
            if (i < n-1) {
                dup2(pipefd[1], STDOUT_FILENO);
            }
            // close fds in child
            if (pipefd[0] != -1) close(pipefd[0]);
            if (pipefd[1] != -1) close(pipefd[1]);
            if (prev_fd != -1) close(prev_fd);
            // redirections
            setup_redirections(cmds[i]);
            exec_cmd_argv(cmds[i]);
            // never returns
        }
        // parent
        // set process group in parent too
        if (pgid == 0) pgid = pid;
        setpgid(pid, pgid);
        pids.push_back(pid);
        if (prev_fd != -1) close(prev_fd);
        if (pipefd[1] != -1) close(pipefd[1]);
        prev_fd = (pipefd[0] != -1) ? pipefd[0] : -1;
    }
    if (pids.empty()) return;
    if (background) {
        // background: report pgid and do not wait
        printf("[Background] %d\n", (int)pgid);
        return;
    }

    // foreground: set fg_pgid so signal handlers forward signals
    fg_pgid = (sig_atomic_t)pgid;
    for (pid_t p: pids) {
        int st = 0; waitpid(p, &st, 0);
    }
    fg_pgid = 0;
}

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
    execute_pipeline(cmds, background);
}
