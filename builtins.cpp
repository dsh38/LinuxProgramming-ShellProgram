#include "builtins.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <fcntl.h>
#include <glob.h>
#include <time.h>
#include <errno.h>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>

static void mode_to_str(mode_t mode, char *out) {
    const char chars[] = "rwxrwxrwx";
    for (int i = 0; i < 9; i++) out[i] = (mode & (1 << (8 - i))) ? chars[i] : '-';
    out[9] = '\0';
}

static int list_directory_cpp(const std::string &path, bool show_all, bool long_format) {
    DIR *dir = opendir(path.c_str());
    if (!dir) { perror("ls"); return 1; }
    struct dirent *entry;
    std::vector<std::string> names;
    std::vector<struct stat> stats;
    long total_blocks = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (!show_all && entry->d_name[0] == '.') continue;
        names.emplace_back(entry->d_name);
        struct stat st;
        std::string full = path + "/" + entry->d_name;
        if (lstat(full.c_str(), &st) == 0) total_blocks += st.st_blocks;
        stats.push_back(st);
    }
    // sort by name
    std::vector<int> idx(names.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b){ return names[a] < names[b]; });

    if (long_format) {
        long total = total_blocks / 2;
        std::cout << "total " << total << "\n";
        for (size_t k = 0; k < idx.size(); ++k) {
            int i = idx[k];
            struct stat &st = stats[i];
            char perms[16]; mode_to_str(st.st_mode, perms);
            char filetype = S_ISDIR(st.st_mode) ? 'd' : (S_ISLNK(st.st_mode) ? 'l' : '-');
            struct passwd *pw = getpwuid(st.st_uid);
            struct group *gr = getgrgid(st.st_gid);
            const char *owner = pw ? pw->pw_name : "?";
            const char *group = gr ? gr->gr_name : "?";
            char timebuf[64]; struct tm *mt = localtime(&st.st_mtime);
            strftime(timebuf, sizeof(timebuf), "%b %e %H:%M", mt);
            std::string name = names[i];
            if (S_ISLNK(st.st_mode)) {
                char linktarget[1024] = "";
                std::string full = path + "/" + name;
                ssize_t rl = readlink(full.c_str(), linktarget, sizeof(linktarget)-1);
                if (rl >= 0) linktarget[rl] = '\0';
                char typeperm[32]; snprintf(typeperm, sizeof(typeperm), "%c%s", filetype, perms);
                printf("%-11s %ld %s %s %5ld %s %s -> %s\n",
                       typeperm, (long)st.st_nlink, owner, group, (long)st.st_size, timebuf, name.c_str(), linktarget);
            } else if (S_ISDIR(st.st_mode)) {
                char typeperm[32]; snprintf(typeperm, sizeof(typeperm), "%c%s", filetype, perms);
                printf("%-11s %ld %s %s %5ld %s \033[1;37;44m%s\033[0m\n",
                       typeperm, (long)st.st_nlink, owner, group, (long)st.st_size, timebuf, name.c_str());
            } else {
                char typeperm[32]; snprintf(typeperm, sizeof(typeperm), "%c%s", filetype, perms);
                printf("%-11s %ld %s %s %5ld %s %s\n",
                       typeperm, (long)st.st_nlink, owner, group, (long)st.st_size, timebuf, name.c_str());
            }
        }
    } else {
        for (int i : idx) std::cout << names[i] << "\n";
    }
    closedir(dir);
    return 0;
}

int ls_builtin(const CommandLine &cl) {
    bool show_all = false;
    bool long_format = false;
    size_t argi = 0;
    if (cl.argv.size() >= 2 && !cl.argv[1].empty() && cl.argv[1][0] == '-') {
        for (size_t j = 1; j < cl.argv[1].size(); ++j) {
            char c = cl.argv[1][j];
            if (c == 'a') show_all = true;
            else if (c == 'l') long_format = true;
            else {
                // fallback: execute system ls
                std::string cmd;
                for (size_t k = 0; k < cl.argv.size(); ++k) {
                    if (k) cmd += ' ';
                    cmd += cl.argv[k];
                }
                system(cmd.c_str());
                return 1;
            }
        }
        argi = 1 + 1;
    } else argi = 1;

    std::vector<std::string> targets;
    for (size_t i = argi; i < cl.argv.size(); ++i) targets.push_back(cl.argv[i]);
    if (targets.empty()) targets.push_back(".");

    if (targets.size() == 1) {
        struct stat st;
        const std::string &t = targets[0];
        if (lstat(t.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            return list_directory_cpp(t, show_all, long_format);
        }
        // else fall through to single file printing below
    }

    for (size_t ti = 0; ti < targets.size(); ++ti) {
        const std::string &target = targets[ti];
        struct stat st;
        if (lstat(target.c_str(), &st) < 0) {
            fprintf(stderr, "ls: %s: %s\n", target.c_str(), strerror(errno));
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (targets.size() > 1) printf("%s:\n", target.c_str());
            list_directory_cpp(target, show_all, long_format);
        } else {
            if (long_format) {
                char perms[16]; mode_to_str(st.st_mode, perms);
                char filetype = S_ISDIR(st.st_mode) ? 'd' : (S_ISLNK(st.st_mode) ? 'l' : '-');
                struct passwd *pw = getpwuid(st.st_uid);
                struct group *gr = getgrgid(st.st_gid);
                const char *owner = pw ? pw->pw_name : "?";
                const char *group = gr ? gr->gr_name : "?";
                char timebuf[64]; struct tm *mt = localtime(&st.st_mtime);
                strftime(timebuf, sizeof(timebuf), "%b %e %H:%M", mt);
                if (S_ISLNK(st.st_mode)) {
                    char linktarget[1024] = "";
                    ssize_t rl = readlink(target.c_str(), linktarget, sizeof(linktarget)-1);
                    if (rl >= 0) linktarget[rl] = '\0';
                    char typeperm[32]; snprintf(typeperm, sizeof(typeperm), "%c%s", filetype, perms);
                    printf("%-11s %ld %s %s %5ld %s %s -> %s\n",
                           typeperm, (long)st.st_nlink, owner, group, (long)st.st_size, timebuf, target.c_str(), linktarget);
                } else if (S_ISDIR(st.st_mode)) {
                    char typeperm[32]; snprintf(typeperm, sizeof(typeperm), "%c%s", filetype, perms);
                    printf("%-11s %ld %s %s %5ld %s \033[1;37;44m%s\033[0m\n",
                           typeperm, (long)st.st_nlink, owner, group, (long)st.st_size, timebuf, target.c_str());
                } else {
                    char typeperm[32]; snprintf(typeperm, sizeof(typeperm), "%c%s", filetype, perms);
                    printf("%-11s %ld %s %s %5ld %s %s\n",
                           typeperm, (long)st.st_nlink, owner, group, (long)st.st_size, timebuf, target.c_str());
                }
            } else {
                printf("%s\n", target.c_str());
            }
        }
    }

    return 0;
}

// grep builtin: perform parent-side globbing and insert --color=always if user
// didn't provide a color option, then exec grep with the final argv.
int grep_builtin(const CommandLine &cl) {
    if (cl.argv.empty()) return 1;
    // expand args (preserve argv[0] as program name)
    std::vector<std::string> expanded;
    for (size_t i = 0; i < cl.argv.size(); ++i) {
        const std::string &a = cl.argv[i];
        // do not expand the program name
        if (i == 0) { expanded.push_back(a); continue; }
        if (a.find_first_of("*?[") == std::string::npos) {
            expanded.push_back(a);
            continue;
        }
        glob_t g; memset(&g, 0, sizeof(g));
        int ret = glob(a.c_str(), GLOB_NOCHECK, NULL, &g);
        if (ret == 0) {
            for (size_t j = 0; j < g.gl_pathc; ++j) expanded.emplace_back(g.gl_pathv[j]);
            globfree(&g);
        } else {
            expanded.push_back(a);
        }
    }

    // check whether user provided --color
    bool has_color = false;
    for (size_t i = 1; i < expanded.size(); ++i) {
        if (expanded[i].find("--color") != std::string::npos) { has_color = true; break; }
    }

    std::vector<std::string> final_argv;
    final_argv.push_back(expanded[0]);
    if (!has_color) final_argv.push_back("--color=always");
    for (size_t i = 1; i < expanded.size(); ++i) final_argv.push_back(expanded[i]);

    // prepare C-style argv
    std::vector<char*> cargv;
    for (auto &s : final_argv) cargv.push_back(const_cast<char*>(s.c_str()));
    cargv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0) {
        execvp(cargv[0], cargv.data());
        perror("execvp");
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

// register builtins at load time
#include "builtin_registry.h"

static void register_my_builtins();
struct __builtin_registrar { __builtin_registrar() { register_my_builtins(); } } __builtin_registrar_instance;

static void register_my_builtins() {
    BuiltinRegistry::instance().registerBuiltin("ls", [](const CommandLine &cl){ return ls_builtin(cl); });
    BuiltinRegistry::instance().registerBuiltin("grep", [](const CommandLine &cl){ return grep_builtin(cl); });
    BuiltinRegistry::instance().registerBuiltin("cp", [](const CommandLine &cl){ return cp_builtin(cl); });
    BuiltinRegistry::instance().registerBuiltin("mv", [](const CommandLine &cl){ return mv_builtin(cl); });
    BuiltinRegistry::instance().registerBuiltin("rm", [](const CommandLine &cl){ return rm_builtin(cl); });
    BuiltinRegistry::instance().registerBuiltin("ln", [](const CommandLine &cl){ return ln_builtin(cl); });
    BuiltinRegistry::instance().registerBuiltin("mkdir", [](const CommandLine &cl){ return mkdir_builtin(cl); });
    BuiltinRegistry::instance().registerBuiltin("rmdir", [](const CommandLine &cl){ return rmdir_builtin(cl); });
    BuiltinRegistry::instance().registerBuiltin("cat", [](const CommandLine &cl){ return cat_builtin(cl); });
}

// Simple implementations for common file-operation builtins.
// These operate in the parent process and expect args to be
// already expanded by the caller where appropriate.

static std::vector<std::string> expand_args_glob(const CommandLine &cl) {
    // Return only operands (skip argv[0] which is the program name)
    std::vector<std::string> out;
    for (size_t idx = 1; idx < cl.argv.size(); ++idx) {
        const auto &a = cl.argv[idx];
        if (a.find_first_of("*?[") != std::string::npos) {
            glob_t g; memset(&g, 0, sizeof(g));
            if (glob(a.c_str(), 0, NULL, &g) == 0) {
                for (size_t i = 0; i < g.gl_pathc; ++i)
                    out.emplace_back(g.gl_pathv[i]);
            }
            globfree(&g);
        } else {
            out.push_back(a);
        }
    }
    return out;
}

int cp_builtin(const CommandLine &cl) {
    if (cl.argv.size() < 3) {
        fprintf(stderr, "cp: missing operand\n");
        return 2;
    }
    auto args = expand_args_glob(cl);
    if (args.size() < 2) {
        fprintf(stderr, "cp: missing operand after glob expansion\n");
        return 2;
    }
    std::string dest = args.back();
    struct stat st;
    bool dest_is_dir = (stat(dest.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
    int ret = 0;
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        const std::string &src = args[i];
        std::string outpath = dest_is_dir ? (dest + "/" + src.substr(src.find_last_of('/') + 1)) : dest;
        int infd = open(src.c_str(), O_RDONLY);
        if (infd < 0) { perror((std::string("cp: ")+src).c_str()); ret = 1; continue; }
        int outfd = open(outpath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (outfd < 0) { perror((std::string("cp: ")+outpath).c_str()); close(infd); ret = 1; continue; }
        char buf[8192];
        ssize_t n;
        while ((n = read(infd, buf, sizeof(buf))) > 0) {
            if (write(outfd, buf, n) != n) { perror((std::string("cp: ")+outpath).c_str()); ret = 1; break; }
        }
        close(infd); close(outfd);
    }
    return ret;
}

int mv_builtin(const CommandLine &cl) {
    if (cl.argv.size() < 3) { fprintf(stderr, "mv: missing operand\n"); return 2; }
    auto args = expand_args_glob(cl);
    if (args.size() < 2) { fprintf(stderr, "mv: missing operand after glob expansion\n"); return 2; }
    std::string dest = args.back();
    struct stat st;
    bool dest_is_dir = (stat(dest.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
    int ret = 0;
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        const std::string &src = args[i];
        std::string outpath = dest_is_dir ? (dest + "/" + src.substr(src.find_last_of('/') + 1)) : dest;
        if (rename(src.c_str(), outpath.c_str()) != 0) { perror((std::string("mv: ")+src).c_str()); ret = 1; }
    }
    return ret;
}

int rm_builtin(const CommandLine &cl) {
    if (cl.argv.size() < 2) { fprintf(stderr, "rm: missing operand\n"); return 2; }
    auto args = expand_args_glob(cl);
    int ret = 0;
    for (const auto &p : args) {
        if (unlink(p.c_str()) != 0) { perror((std::string("rm: ")+p).c_str()); ret = 1; }
    }
    return ret;
}

int ln_builtin(const CommandLine &cl) {
    if (cl.argv.size() < 3) { fprintf(stderr, "ln: missing operand\n"); return 2; }
    bool symbolic = false;
    size_t idx = 1;
    if (cl.argv[1] == "-s") { symbolic = true; idx = 2; }
    if (cl.argv.size() - idx < 2) { fprintf(stderr, "ln: missing operand\n"); return 2; }
    std::string target = cl.argv[idx];
    std::string linkname = cl.argv[idx+1];
    int r = symbolic ? symlink(target.c_str(), linkname.c_str()) : link(target.c_str(), linkname.c_str());
    if (r != 0) { perror("ln"); return 1; }
    return 0;
}

int mkdir_builtin(const CommandLine &cl) {
    if (cl.argv.size() < 2) { fprintf(stderr, "mkdir: missing operand\n"); return 2; }
    int ret = 0;
    for (size_t i = 1; i < cl.argv.size(); ++i) {
        if (mkdir(cl.argv[i].c_str(), 0777) != 0) { perror((std::string("mkdir: ")+cl.argv[i]).c_str()); ret = 1; }
    }
    return ret;
}

int rmdir_builtin(const CommandLine &cl) {
    if (cl.argv.size() < 2) { fprintf(stderr, "rmdir: missing operand\n"); return 2; }
    int ret = 0;
    for (size_t i = 1; i < cl.argv.size(); ++i) {
        if (rmdir(cl.argv[i].c_str()) != 0) { perror((std::string("rmdir: ")+cl.argv[i]).c_str()); ret = 1; }
    }
    return ret;
}

int cat_builtin(const CommandLine &cl) {
    if (cl.argv.size() < 2) { fprintf(stderr, "cat: missing operand\n"); return 2; }
    int ret = 0;
    for (size_t i = 1; i < cl.argv.size(); ++i) {
        const std::string &p = cl.argv[i];
        int fd = open(p.c_str(), O_RDONLY);
        if (fd < 0) { perror((std::string("cat: ")+p).c_str()); ret = 1; continue; }
        char buf[8192]; ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            if (write(STDOUT_FILENO, buf, n) != n) { perror("cat"); ret = 1; break; }
        }
        close(fd);
    }
    return ret;
}
