// command.cpp - implementations for SimpleCommand and PipelineCommand
#include "command.h"
#include "runtime_state.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>

SimpleCommand::SimpleCommand(const CommandLine &cl) : cl_(cl) {}

static void exec_cmd_argv_local(const CommandLine &cl) {
    if (cl.argv.empty()) _exit(0);
    std::vector<char*> cargs;
    for (const auto &s : cl.argv) cargs.push_back(const_cast<char*>(s.c_str()));
    cargs.push_back(nullptr);
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
    execvp(cargs[0], cargs.data());
    perror("execvp");
    _exit(127);
}

int SimpleCommand::execute(bool background) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0) {
        // child: setpgid to its own pid
        setpgid(0, 0);
        exec_cmd_argv_local(cl_);
    }
    // parent
    setpgid(pid, pid);
    if (background) {
        printf("[Background] %d\n", (int)pid);
        return 0;
    }
    // foreground: set fg_pgid and wait
    fg_pgid = (sig_atomic_t)pid;
    int status = 0; waitpid(pid, &status, 0);
    fg_pgid = 0;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

PipelineCommand::PipelineCommand(std::vector<CommandLine> stages) : stages_(std::move(stages)) {}

int PipelineCommand::execute(bool background) {
    int n = stages_.size();
    if (n == 0) return 0;
    int prev_fd = -1;
    std::vector<pid_t> pids;
    pid_t pgid = 0;
    for (int i = 0; i < n; ++i) {
        int pipefd[2] = {-1, -1};
        if (i < n-1) {
            if (pipe(pipefd) < 0) { perror("pipe"); return 1; }
        }
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 1; }
        if (pid == 0) {
            // child
            if (pgid == 0) setpgid(0, 0); else setpgid(0, pgid);
            if (prev_fd != -1) dup2(prev_fd, STDIN_FILENO);
            if (i < n-1) dup2(pipefd[1], STDOUT_FILENO);
            if (pipefd[0] != -1) close(pipefd[0]);
            if (pipefd[1] != -1) close(pipefd[1]);
            if (prev_fd != -1) close(prev_fd);
            // redirections
            if (!stages_[i].input_file.empty()) {
                int fd = open(stages_[i].input_file.c_str(), O_RDONLY);
                if (fd < 0) { perror("input redir"); _exit(127); }
                dup2(fd, STDIN_FILENO); close(fd);
            }
            if (!stages_[i].output_file.empty()) {
                int fd = open(stages_[i].output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) { perror("output redir"); _exit(127); }
                dup2(fd, STDOUT_FILENO); close(fd);
            }
            std::vector<char*> cargs;
            for (const auto &s : stages_[i].argv) cargs.push_back(const_cast<char*>(s.c_str()));
            cargs.push_back(nullptr);
            execvp(cargs[0], cargs.data());
            perror("execvp");
            _exit(127);
        }
        if (pgid == 0) pgid = pid;
        setpgid(pid, pgid);
        pids.push_back(pid);
        if (prev_fd != -1) close(prev_fd);
        if (pipefd[1] != -1) close(pipefd[1]);
        prev_fd = (pipefd[0] != -1) ? pipefd[0] : -1;
    }
    if (pids.empty()) return 0;
    if (background) {
        printf("[Background] %d\n", (int)pgid);
        return 0;
    }
    fg_pgid = (sig_atomic_t)pgid;
    for (pid_t p : pids) { int st; waitpid(p, &st, 0); }
    fg_pgid = 0;
    return 0;
}
