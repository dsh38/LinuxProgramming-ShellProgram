#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include <glob.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
// readline for line editing, history and completion
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_CMD_LEN 1024
#define MAX_ARGS 64

// Debug: log exec argv for analysis
static void log_argv(const char *stage, char **argv) {
    FILE *f = fopen("/tmp/teamshell_exec_log.txt", "a");
    if (!f) return;
    fprintf(f, "--- %s (pid=%d) ---\n", stage, getpid());
    for (int i = 0; argv && argv[i]; i++) {
        fprintf(f, "argv[%d]=%s\n", i, argv[i]);
    }
    fprintf(f, "--- end %s ---\n\n", stage);
    fclose(f);
}

// forward declarations for glob helpers (used by some builtins)
char **expand_args(char **args);
void free_expanded_args(char **pargs);

// 전역 변수
pid_t fg_pid = 0;  // 포어그라운드 프로세스 PID
char prev_cwd[1024] = ""; // 이전 작업 디렉터리 (for cd -)

// 시그널 핸들러
void sigint_handler(int sig) {
    (void)sig;
    printf("\n");
    if (fg_pid > 0) {
        kill(fg_pid, SIGINT);
    }
}

void sigquit_handler(int sig) {
    (void)sig;
    printf("\n");
    if (fg_pid > 0) {
        kill(fg_pid, SIGTSTP);
    }
}

// 내장 명령어 구현
// helper: convert mode to rwxr-xr-x style
static void mode_to_str(mode_t mode, char *out) {
    const char chars[] = "rwxrwxrwx";
    for (int i = 0; i < 9; i++) {
        out[i] = (mode & (1 << (8 - i))) ? chars[i] : '-';
    }
    out[9] = '\0';
}

// Helper: list directory entries sorted, print `total` for long format and symlink targets
static int list_directory(const char *path, int show_all, int long_format) {
    DIR *dir = opendir(path);
    if (!dir) { perror("ls"); return 1; }

    struct dirent *entry;
    // collect names and stats
    size_t cap = 64, cnt = 0;
    char **names = malloc(sizeof(char*) * cap);
    struct stat *stats = malloc(sizeof(struct stat) * cap);
    if (!names || !stats) {
        closedir(dir);
        free(names); free(stats);
        return 1;
    }

    long total_blocks = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (!show_all && entry->d_name[0] == '.') continue;
        if (cnt + 1 >= cap) {
            cap *= 2;
            names = realloc(names, sizeof(char*) * cap);
            stats = realloc(stats, sizeof(struct stat) * cap);
            if (!names || !stats) { closedir(dir); return 1; }
        }
        names[cnt] = strdup(entry->d_name);
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
        if (lstat(fullpath, &stats[cnt]) < 0) {
            // zero out on error
            memset(&stats[cnt], 0, sizeof(struct stat));
        } else {
            total_blocks += stats[cnt].st_blocks;
        }
        cnt++;
    }

    // sort by name
    if (cnt > 1) {
        // to sort names and keep stats aligned, build indices
        int *indices = malloc(sizeof(int) * cnt);
        for (size_t i = 0; i < cnt; i++) indices[i] = i;
        int cmpi(const void *x, const void *y) {
            int ix = *(const int*)x;
            int iy = *(const int*)y;
            return strcmp(names[ix], names[iy]);
        }
        qsort(indices, cnt, sizeof(int), cmpi);

        // create ordered arrays
        char **onames = malloc(sizeof(char*) * cnt);
        struct stat *ostats = malloc(sizeof(struct stat) * cnt);
        for (size_t i = 0; i < cnt; i++) {
            onames[i] = names[indices[i]];
            ostats[i] = stats[indices[i]];
        }
        // free originals container but not strings (ownership moved)
        free(names); free(stats); free(indices);
        names = onames; stats = ostats;
    }

    if (long_format) {
        // print total in 1K-blocks (st_blocks is 512-byte blocks on many systems)
        long total = total_blocks / 2;
        printf("total %ld\n", total);
        for (size_t i = 0; i < cnt; i++) {
            struct stat *st = &stats[i];
            char perms[16]; mode_to_str(st->st_mode, perms);
            char filetype = S_ISDIR(st->st_mode) ? 'd' : (S_ISLNK(st->st_mode) ? 'l' : '-');
            struct passwd *pw = getpwuid(st->st_uid);
            struct group *gr = getgrgid(st->st_gid);
            const char *owner = pw ? pw->pw_name : "?";
            const char *group = gr ? gr->gr_name : "?";
            char timebuf[64]; struct tm *mt = localtime(&st->st_mtime);
            strftime(timebuf, sizeof(timebuf), "%b %e %H:%M", mt);
                 if (S_ISLNK(st->st_mode)) {
                  char linktarget[1024] = "";
                  char fullpath[1024]; snprintf(fullpath, sizeof(fullpath), "%s/%s", path, names[i]);
                  ssize_t rl = readlink(fullpath, linktarget, sizeof(linktarget)-1);
                  if (rl >= 0) { linktarget[rl] = '\0'; }
                  char typeperm[32]; snprintf(typeperm, sizeof(typeperm), "%c%s", filetype, perms);
                  printf("%-11s %ld %s %s %5ld %s %s -> %s\n",
                      typeperm, (long)st->st_nlink, owner, group, (long)st->st_size, timebuf, names[i], linktarget);
            } else if (S_ISDIR(st->st_mode)) {
                  char typeperm[32]; snprintf(typeperm, sizeof(typeperm), "%c%s", filetype, perms);
                  printf("%-11s %ld %s %s %5ld %s \033[1;37;44m%s\033[0m\n",
                      typeperm, (long)st->st_nlink, owner, group, (long)st->st_size, timebuf, names[i]);
            } else {
                  char typeperm[32]; snprintf(typeperm, sizeof(typeperm), "%c%s", filetype, perms);
                  printf("%-11s %ld %s %s %5ld %s %s\n",
                      typeperm, (long)st->st_nlink, owner, group, (long)st->st_size, timebuf, names[i]);
            }
        }
    } else {
            for (size_t i = 0; i < cnt; i++) {
                printf("%s\n", names[i]);
            }
    }

    for (size_t i = 0; i < cnt; i++) free(names[i]);
    free(names); free(stats);
    closedir(dir);
    return 1;
}

int cmd_ls(char **args) {
    int show_all = 0;
    int long_format = 0;
    
    // parse simple flags if present (e.g., -a, -l, -la, -al)
    int argi = 1;
    if (args[1] != NULL && args[1][0] == '-') {
        for (size_t i = 1; args[1][i]; i++) {
            if (args[1][i] == 'a') show_all = 1;
            else if (args[1][i] == 'l') long_format = 1;
            else {
                // unknown option: delegate to system ls to preserve behavior
                pid_t pid = fork();
                if (pid == 0) {
                    execvp("ls", args);
                    perror("ls");
                    exit(1);
                }
                waitpid(pid, NULL, 0);
                return 1;
            }
        }
        argi = 2;
    }

    // Collect targets (if none, target is current directory)
    int targets_start = argi;
    int targets_count = 0;
    for (int i = targets_start; args[i] != NULL; i++) targets_count++;

    // If no targets, list current directory
    if (targets_count == 0) {
        return list_directory(".", show_all, long_format);
    }

    // There are explicit targets: handle each one separately
    int printed_any = 0;
    for (int ti = 0; ti < targets_count; ti++) {
        const char *target = args[targets_start + ti];
        struct stat st;
        if (lstat(target, &st) < 0) {
            // If target doesn't exist, print error like ls
            fprintf(stderr, "ls: %s: ", target);
            perror("");
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            // Directory: if multiple targets, print header
            if (targets_count > 1) printf("%s:\n", target);
            list_directory(target, show_all, long_format);
        } else {
            // File: if long format, print long info for file; otherwise print name
            if (long_format) {
                char perms[16]; mode_to_str(st.st_mode, perms);
                char filetype = S_ISDIR(st.st_mode) ? 'd' : (S_ISLNK(st.st_mode) ? 'l' : '-');
                struct passwd *pw = getpwuid(st.st_uid);
                struct group *gr = getgrgid(st.st_gid);
                const char *owner = pw ? pw->pw_name : "?";
                const char *group = gr ? gr->gr_name : "?";
                char timebuf[64]; struct tm *mt = localtime(&st.st_mtime);
                strftime(timebuf, sizeof(timebuf), "%b %e %H:%M", mt);
                const char *display = target;
                if (S_ISLNK(st.st_mode)) {
                    char linktarget[1024] = "";
                    ssize_t rl = readlink(target, linktarget, sizeof(linktarget)-1);
                    if (rl >= 0) linktarget[rl] = '\0';
                              {
                               char typeperm[32]; snprintf(typeperm, sizeof(typeperm), "%c%s", filetype, perms);
                               printf("%-11s %ld %s %s %5ld %s %s -> %s\n",
                                   typeperm, (long)st.st_nlink, owner, group, (long)st.st_size, timebuf, display, linktarget);
                              }
                } else if (S_ISDIR(st.st_mode)) {
                              {
                               char typeperm[32]; snprintf(typeperm, sizeof(typeperm), "%c%s", filetype, perms);
                               printf("%-11s %ld %s %s %5ld %s \033[1;37;44m%s\033[0m\n",
                                   typeperm, (long)st.st_nlink, owner, group, (long)st.st_size, timebuf, display);
                              }
                } else {
                              {
                               char typeperm[32]; snprintf(typeperm, sizeof(typeperm), "%c%s", filetype, perms);
                               printf("%-11s %ld %s %s %5ld %s %s\n",
                                   typeperm, (long)st.st_nlink, owner, group, (long)st.st_size, timebuf, display);
                              }
                }
            } else {
                // simple print (color directories but this is a file)
                const char *display = target;
                printf("%s  ", display);
            }
        }
        printed_any = 1;
    }

    // print trailing newline for plain-file simple prints
    if (printed_any && !long_format) printf("\n");
    return 1;
}

int cmd_pwd(char **args) {
    (void)args;
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("pwd");
    }
    return 1;
}

int cmd_cd(char **args) {
    char oldcwd[1024];
    if (!getcwd(oldcwd, sizeof(oldcwd))) strncpy(oldcwd, "", sizeof(oldcwd));

    const char *target = NULL;
    char buf[1024];

    if (args[1] == NULL) {
        // no arg -> go to HOME
        target = getenv("HOME");
        if (!target) target = "/";
    } else if (strcmp(args[1], "-") == 0) {
        // cd - -> previous directory
        if (prev_cwd[0] == '\0') {
            fprintf(stderr, "cd: OLDPWD not set\n");
            return 1;
        }
        target = prev_cwd;
    } else if (args[1][0] == '~') {
        // expand ~ to HOME
        const char *home = getenv("HOME");
        if (!home) home = "/";
        if (args[1][1] == '/' || args[1][1] == '\0') {
            snprintf(buf, sizeof(buf), "%s%s", home, args[1] + 1);
            target = buf;
        } else {
            // ~user not supported: fallback to literal
            target = args[1];
        }
    } else {
        target = args[1];
    }

    if (target == NULL) {
        fprintf(stderr, "cd: missing argument\n");
        return 1;
    }

    if (chdir(target) != 0) {
        perror("cd");
    } else {
        // update prev_cwd
        strncpy(prev_cwd, oldcwd, sizeof(prev_cwd)-1);
        prev_cwd[sizeof(prev_cwd)-1] = '\0';
        // if the user used `cd -`, print the new cwd (like bash)
        if (strcmp(args[1] ? args[1] : "", "-") == 0) {
            char newcwd[1024];
            if (getcwd(newcwd, sizeof(newcwd))) printf("%s\n", newcwd);
        }
    }
    return 1;
}

int cmd_mkdir(char **args) {
    pid_t pid = fork();
    if (pid == 0) {
        execvp("mkdir", args);
        perror("mkdir");
        exit(1);
    }
    waitpid(pid, NULL, 0);
    return 1;
}

int cmd_rmdir(char **args) {
    pid_t pid = fork();
    if (pid == 0) {
        execvp("rmdir", args);
        perror("rmdir");
        exit(1);
    }
    waitpid(pid, NULL, 0);
    return 1;
}

int cmd_ln(char **args) {
    pid_t pid = fork();
    if (pid == 0) {
        execvp("ln", args);
        perror("ln");
        exit(1);
    }
    waitpid(pid, NULL, 0);
    return 1;
}

int cmd_cp(char **args) {
    pid_t pid = fork();
    if (pid == 0) {
        execvp("cp", args);
        perror("cp");
        exit(1);
    }
    waitpid(pid, NULL, 0);
    return 1;
}

int cmd_rm(char **args) {
    pid_t pid = fork();
    if (pid == 0) {
        execvp("rm", args);
        perror("rm");
        exit(1);
    }
    waitpid(pid, NULL, 0);
    return 1;
}

int cmd_mv(char **args) {
    pid_t pid = fork();
    if (pid == 0) {
        execvp("mv", args);
        perror("mv");
        exit(1);
    }
    waitpid(pid, NULL, 0);
    return 1;
}

int cmd_cat(char **args) {
    pid_t pid = fork();
    if (pid == 0) {
        execvp("cat", args);
        perror("cat");
        exit(1);
    }
    waitpid(pid, NULL, 0);
    return 1;
}

int cmd_grep(char **args) {
    // 글로빙 확장 지원
    char **expanded = expand_args(args);

    // 사용자가 이미 --color 옵션을 전달했는지 확인
    int has_color_opt = 0;
    if (expanded) {
        for (int i = 0; expanded[i] != NULL; i++) {
            if (strstr(expanded[i], "--color") != NULL) { has_color_opt = 1; break; }
        }
    }

    // 새로운 argv 구성: grep [--color=always] rest...
    char **newargv = NULL;
    int need_free_newargv = 0;
    if (!has_color_opt) {
        // count expanded args
        int n = 0;
        if (expanded) {
            while (expanded[n]) n++;
        } else {
            while (args[n]) n++;
        }
        // allocate pointers for n + 2 (insert color) + 1(NULL)
        newargv = malloc(sizeof(char*) * (n + 3));
        if (newargv) {
            need_free_newargv = 1;
            int idx = 0;
            // program name
            if (expanded && expanded[0]) newargv[idx++] = strdup(expanded[0]);
            else newargv[idx++] = strdup(args[0]);
            // insert color flag
            newargv[idx++] = strdup("--color=always");
            // copy rest
            char **src = expanded ? &expanded[1] : &args[1];
            for (int j = 0; src[j] != NULL; j++) {
                newargv[idx++] = strdup(src[j]);
            }
            newargv[idx] = NULL;
        }
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Log argv for grep invocation (child inherits memory but log from parent earlier)
        if (newargv) log_argv("cmd_grep:newargv", newargv);
        else if (expanded && expanded[0]) log_argv("cmd_grep:expanded", expanded);
        else log_argv("cmd_grep:args", args);

        if (newargv) {
            execvp(newargv[0], newargv);
        } else if (expanded && expanded[0]) {
            execvp(expanded[0], expanded);
        } else {
            execvp(args[0], args);
        }
        perror("grep");
        exit(1);
    }
    waitpid(pid, NULL, 0);

    if (expanded) free_expanded_args(expanded);
    if (need_free_newargv && newargv) {
        for (int i = 0; newargv[i]; i++) free(newargv[i]);
        free(newargv);
    }
    return 1;
}

// 명령어 파싱
void parse_command(char *cmd, char **args, int *background, 
                   char **input_file, char **output_file) {
    int i = 0;
    *background = 0;
    *input_file = NULL;
    *output_file = NULL;

    char *p = cmd;
    while (*p) {
        // skip whitespace
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        // background token
        if (*p == '&') {
            *background = 1;
            p++;
            continue;
        }

        // input redirection
        if (*p == '<') {
            p++;
            while (*p && isspace((unsigned char)*p)) p++;
            if (!*p) break;
            char *start;
            if (*p == '"' || *p == '\'') {
                char q = *p;
                start = ++p;
                while (*p && *p != q) p++;
                if (*p == q) {
                    *p = '\0';
                    p++;
                }
            } else {
                start = p;
                while (*p && !isspace((unsigned char)*p)) p++;
                if (*p) { *p = '\0'; p++; }
            }
            *input_file = start;
            continue;
        }

        // output redirection
        if (*p == '>') {
            p++;
            while (*p && isspace((unsigned char)*p)) p++;
            if (!*p) break;
            char *start;
            if (*p == '"' || *p == '\'') {
                char q = *p;
                start = ++p;
                while (*p && *p != q) p++;
                if (*p == q) {
                    *p = '\0';
                    p++;
                }
            } else {
                start = p;
                while (*p && !isspace((unsigned char)*p)) p++;
                if (*p) { *p = '\0'; p++; }
            }
            *output_file = start;
            continue;
        }

        // normal argument (support quoted strings)
        char *start;
        if (*p == '"' || *p == '\'') {
            char q = *p;
            start = ++p;
            while (*p && *p != q) p++;
            if (*p == q) {
                *p = '\0';
                p++;
            }
            args[i++] = start;
        } else {
            start = p;
            while (*p && !isspace((unsigned char)*p) && *p != '<' && *p != '>' && *p != '&') p++;
            if (*p) {
                *p = '\0';
                p++;
            }
            args[i++] = start;
        }
    }
    args[i] = NULL;
}

// Preprocess shorthand like "cd.." or "cd../path" -> "cd .." so parser handles it
void preprocess_cd_shorthand(char *cmd) {
    if (!cmd) return;
    char *p = cmd;
    while (*p) {
        // detect beginning of token: start of string or previous is whitespace or semicolon
        if ((p == cmd || isspace((unsigned char)*(p-1)) || *(p-1) == ';') && p[0] == 'c' && p[1] == 'd') {
            char *q = p + 2;
            // if immediately followed by non-space and not end, insert a space
            if (*q != '\0' && !isspace((unsigned char)*q)) {
                size_t tail_len = strlen(q);
                // ensure we don't overflow buffer; MAX_CMD_LEN is upper bound for buffer elsewhere
                if (strlen(cmd) + 1 < MAX_CMD_LEN) {
                    memmove(q + 1, q, tail_len + 1);
                    *q = ' ';
                    // advance pointer after inserted space
                    p = q + 1;
                    continue;
                }
            }
        }
        p++;
    }
}

// 인수 글로빙: 와일드카드(* ? [])를 파일 목록으로 확장
char **expand_args(char **args) {
    if (!args || !args[0]) return NULL;

    size_t cap = 64;
    size_t cnt = 0;
    char **out = malloc(sizeof(char*) * cap);
    if (!out) return NULL;

    for (int i = 0; args[i] != NULL; i++) {
        char *a = args[i];
        // 패턴 문자 검사
        if (strpbrk(a, "*?[]") != NULL) {
            glob_t g;
            int flags = 0; // 기본 동작: GLOB_NOCHECK로 매칭이 없으면 패턴을 그대로 반환하게 할 수 있다.
            int ret = glob(a, flags | GLOB_NOCHECK, NULL, &g);
            if (ret == 0) {
                for (size_t j = 0; j < g.gl_pathc; j++) {
                    if (cnt + 1 >= cap) {
                        cap *= 2;
                        out = realloc(out, sizeof(char*) * cap);
                    }
                    out[cnt++] = strdup(g.gl_pathv[j]);
                }
            }
            globfree(&g);
        } else {
            if (cnt + 1 >= cap) {
                cap *= 2;
                out = realloc(out, sizeof(char*) * cap);
            }
            out[cnt++] = strdup(a);
        }
    }

    // 널 종료
    if (cnt + 1 >= cap) {
        out = realloc(out, sizeof(char*) * (cap + 1));
    }
    out[cnt] = NULL;
    return out;
}

// expand_args로 생성한 배열을 해제
void free_expanded_args(char **pargs) {
    if (!pargs) return;
    for (int i = 0; pargs[i] != NULL; i++) free(pargs[i]);
    free(pargs);
}

// Detect a real pipeline '|' that is not part of '||' and not inside quotes
static int contains_pipe_char(const char *s) {
    if (!s) return 0;
    char quote = 0;
    for (const char *p = s; *p; p++) {
        if (quote) {
            if (*p == quote) quote = 0;
            continue;
        }
        if (*p == '"' || *p == '\'') { quote = *p; continue; }
        if (*p == '|') {
            if (*(p+1) == '|') { p++; continue; }
            return 1;
        }
    }
    return 0;
}

// 파이프 처리
void execute_pipe(char *cmd) {
    char *commands[MAX_ARGS];
    int num_cmds = 0;
    
    // 파이프로 명령어 분리 (인용문자 내부의 '|' 혹은 '||'는 분리하지 않음)
    char *p = cmd;
    char *start = p;
    char quote = 0;
    while (*p && num_cmds < MAX_ARGS) {
        if (quote) {
            if (*p == quote) {
                quote = 0;
            }
            p++;
            continue;
        }
        if (*p == '"' || *p == '\'') {
            quote = *p;
            p++;
            continue;
        }
        if (*p == '|') {
            // if this is '||' (logical OR), do not split
            if (*(p+1) == '|') {
                p += 2; // skip both pipes
                continue;
            }
            // split here
            *p = '\0';
            // trim leading/trailing spaces from start
            while (start && *start && isspace((unsigned char)*start)) start++;
            char *end = p - 1;
            while (end > start && isspace((unsigned char)*end)) { *end = '\0'; end--; }
            commands[num_cmds++] = start;
            p++;
            start = p;
            continue;
        }
        p++;
    }
    // last token
    if (num_cmds < MAX_ARGS && start && *start) {
        // trim
        while (start && *start && isspace((unsigned char)*start)) start++;
        char *end = p - 1;
        while (end > start && isspace((unsigned char)*end)) { *end = '\0'; end--; }
        if (*start) commands[num_cmds++] = start;
    }
    
    if (num_cmds == 1) {
        // 파이프가 없으면 일반 실행
        return;
    }
    
    int pipefds[2 * (num_cmds - 1)];
    
    // 파이프 생성
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipefds + i * 2) < 0) {
            perror("pipe");
            return;
        }
    }
    
    // 각 명령어 실행
    for (int i = 0; i < num_cmds; i++) {
        char *args[MAX_ARGS];
        int background = 0;
        char *input_file = NULL, *output_file = NULL;

        char cmd_copy[MAX_CMD_LEN];
        strcpy(cmd_copy, commands[i]);
        parse_command(cmd_copy, args, &background, &input_file, &output_file);

        // 부모에서 글로빙 수행하여 한 번만 확장하고, 부모가 해제하도록 한다.
        char **expanded = expand_args(args);

        // Log what will be exec'd for this pipeline stage (parent-side)
        if (expanded && expanded[0]) log_argv("execute_pipe:expanded", expanded);
        else log_argv("execute_pipe:args", args);

        pid_t pid = fork();
        if (pid == 0) {
            // Child: handle per-stage input/output redirection first (if any),
            // otherwise hook up pipes.
            if (input_file) {
                int fd = open(input_file, O_RDONLY);
                if (fd < 0) {
                    perror("input redirection");
                    exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            } else if (i > 0) {
                // 받아온 파이프를 표준 입력으로 연결
                dup2(pipefds[(i - 1) * 2], STDIN_FILENO);
            }

            if (output_file) {
                int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    perror("output redirection");
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            } else if (i < num_cmds - 1) {
                // 다음 파이프를 표준 출력으로 연결
                dup2(pipefds[i * 2 + 1], STDOUT_FILENO);
            }

            // 모든 파이프 닫기
            for (int j = 0; j < 2 * (num_cmds - 1); j++) {
                close(pipefds[j]);
            }

            // 자식에서 exec
            if (expanded && expanded[0]) {
                execvp(expanded[0], expanded);
            } else {
                execvp(args[0], args);
            }
            perror("execvp");
            exit(1);
        } else {
            // 부모: expand_args로 만든 메모리 해제
            if (expanded) free_expanded_args(expanded);
        }
    }
    
    // 모든 파이프 닫기
    for (int i = 0; i < 2 * (num_cmds - 1); i++) {
        close(pipefds[i]);
    }
    
    // 모든 자식 프로세스 대기
    for (int i = 0; i < num_cmds; i++) {
        wait(NULL);
    }
}

// 명령어 실행
int execute_command(char **args, int background, 
                   char *input_file, char *output_file) {
    if (args[0] == NULL) {
        return 1;
    }
    // Log received argv in parent for debugging
    log_argv("execute_command:received", args);
    // Perform glob expansion for all commands so builtins (ls, grep, etc.) see expanded args
    char **expanded = expand_args(args);
    char **use_args = expanded ? expanded : args;
    // 지원: 사용자가 'cd..' 같이 공백 없이 쓴 경우를 처리 (check original args)
    if (strncmp(args[0], "cd", 2) == 0 && args[0][2] != '\0') {
        char *tmp_args[3];
        tmp_args[0] = "cd";
        tmp_args[1] = args[0] + 2;
        tmp_args[2] = NULL;
        if (expanded) free_expanded_args(expanded);
        return cmd_cd(tmp_args);
    }

    // exit 명령어 처리
    if (strcmp(use_args[0], "exit") == 0) {
        if (expanded) free_expanded_args(expanded);
        return 0;
    }

    // 내장 명령어 처리 (use_args를 사용하여 확장된 인수를 고려)
    if (strcmp(use_args[0], "cd") == 0) {
        if (expanded) { int r = cmd_cd(use_args); free_expanded_args(expanded); return r; }
        return cmd_cd(args);
    } else if (strcmp(use_args[0], "pwd") == 0) {
        if (expanded) { int r = cmd_pwd(use_args); free_expanded_args(expanded); return r; }
        return cmd_pwd(args);
    } else if (strcmp(use_args[0], "ls") == 0) {
        if (expanded) { int r = cmd_ls(use_args); free_expanded_args(expanded); return r; }
        return cmd_ls(args);
    } else if (strcmp(use_args[0], "mkdir") == 0) {
        if (expanded) { int r = cmd_mkdir(use_args); free_expanded_args(expanded); return r; }
        return cmd_mkdir(args);
    } else if (strcmp(use_args[0], "rmdir") == 0) {
        if (expanded) { int r = cmd_rmdir(use_args); free_expanded_args(expanded); return r; }
        return cmd_rmdir(args);
    } else if (strcmp(use_args[0], "ln") == 0) {
        if (expanded) { int r = cmd_ln(use_args); free_expanded_args(expanded); return r; }
        return cmd_ln(args);
    } else if (strcmp(use_args[0], "cp") == 0) {
        if (expanded) { int r = cmd_cp(use_args); free_expanded_args(expanded); return r; }
        return cmd_cp(args);
    } else if (strcmp(use_args[0], "rm") == 0) {
        if (expanded) { int r = cmd_rm(use_args); free_expanded_args(expanded); return r; }
        return cmd_rm(args);
    } else if (strcmp(use_args[0], "mv") == 0) {
        if (expanded) { int r = cmd_mv(use_args); free_expanded_args(expanded); return r; }
        return cmd_mv(args);
    } else if (strcmp(use_args[0], "cat") == 0) {
        if (expanded) { int r = cmd_cat(use_args); free_expanded_args(expanded); return r; }
        return cmd_cat(args);
    } else if (strcmp(use_args[0], "grep") == 0) {
        if (expanded) { int r = cmd_grep(use_args); free_expanded_args(expanded); return r; }
        return cmd_grep(args);
    }

    pid_t pid = fork();
    if (pid == 0) {
        // 입력 재지향
        if (input_file) {
            int fd = open(input_file, O_RDONLY);
            if (fd < 0) {
                perror("input redirection");
                exit(1);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        
        // 출력 재지향
        if (output_file) {
            int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror("output redirection");
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        
        // Log argv before exec for external command (use use_args if set)
        if (use_args && use_args[0]) log_argv("execute_command:expanded", use_args);
        else log_argv("execute_command:args", args);

        execvp(use_args[0], use_args);
        perror("execvp");
        exit(1);
    } else if (pid > 0) {
        if (!background) {
            fg_pid = pid;
            waitpid(pid, NULL, 0);
            fg_pid = 0;
        } else {
            printf("[Background] PID: %d\n", pid);
        }
    } else {
        perror("fork");
    }
    
    // 부모: expand_args로 만든 메모리 해제
    if (expanded) free_expanded_args(expanded);

    return 1;
}

int main() {
    char cmd[MAX_CMD_LEN];
    char *args[MAX_ARGS];
    int background;
    char *input_file, *output_file;

    // 시그널 핸들러 설정
    signal(SIGINT, sigint_handler);   // Ctrl-C
    signal(SIGQUIT, sigquit_handler); // Ctrl-Z (실제로는 Ctrl-\ 이지만 요구사항대로)

    // readline completion 설정: 파일 이름 자동완성 사용 (interactive only)
    rl_attempted_completion_function = NULL;
    rl_attempted_completion_function = (rl_completion_func_t *) (void *) rl_filename_completion_function;

    // If stdin is a TTY, use readline with prompt/history. Otherwise use non-interactive fgets loop.
    if (isatty(STDIN_FILENO)) {
        printf("Simple Shell - Type 'exit' to quit\n");
        while (1) {
            char cwd[1024];
            char *prompt = NULL;
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                int needed = snprintf(NULL, 0, "myshell:%s> ", cwd) + 1;
                prompt = malloc(needed);
                if (prompt) snprintf(prompt, needed, "myshell:%s> ", cwd);
            }
            if (!prompt) prompt = strdup("myshell> ");

            char *line = readline(prompt);
            free(prompt);
            if (!line) break; // EOF (Ctrl-D)

            // empty line
            if (line[0] == '\0') {
                free(line);
                continue;
            }

            // add to history
            add_history(line);

            // copy into cmd buffer for in-place parsing
            strncpy(cmd, line, MAX_CMD_LEN - 1);
            cmd[MAX_CMD_LEN - 1] = '\0';
            // normalize cd shorthand (e.g., "cd.." -> "cd ..") so parse_command handles it
            preprocess_cd_shorthand(cmd);
            free(line);

            // 파이프 확인 (인용이나 '||' 제외한 실제 파이프만 검사)
            if (contains_pipe_char(cmd)) {
                cmd[strcspn(cmd, "\n")] = 0;
                execute_pipe(cmd);
                fflush(stdout);
                fflush(stderr);
                continue;
            }

            // 명령어 파싱
            parse_command(cmd, args, &background, &input_file, &output_file);

            // 명령어 실행
            if (!execute_command(args, background, input_file, output_file)) {
                break;
            }
            fflush(stdout);
            fflush(stderr);
        }
    } else {
        // Non-interactive: read lines from stdin without printing prompts
        while (fgets(cmd, MAX_CMD_LEN, stdin) != NULL) {
            if (cmd[0] == '\n' || cmd[0] == '\0') continue;
            // strip trailing newline
            cmd[strcspn(cmd, "\n")] = '\0';
            preprocess_cd_shorthand(cmd);

            // 파이프 check: if contains a real pipe char, execute_pipe handles the whole cmd
            if (contains_pipe_char(cmd)) {
                execute_pipe(cmd);
                fflush(stdout);
                fflush(stderr);
                continue;
            }

            parse_command(cmd, args, &background, &input_file, &output_file);
            if (!execute_command(args, background, input_file, output_file)) break;
            fflush(stdout);
            fflush(stderr);
        }
    }
    
    /* No goodbye message to avoid polluting non-interactive logs */
    return 0;
}
