#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_CMD_LEN 1024
#define MAX_ARGS 64

// 전역 변수
pid_t fg_pid = 0;  // 포어그라운드 프로세스 PID

// 시그널 핸들러
void sigint_handler(int sig) {
    printf("\n");
    if (fg_pid > 0) {
        kill(fg_pid, SIGINT);
    }
}

void sigquit_handler(int sig) {
    printf("\n");
    if (fg_pid > 0) {
        kill(fg_pid, SIGTSTP);
    }
}

// 내장 명령어 구현
int cmd_ls(char **args) {
    pid_t pid = fork();
    if (pid == 0) {
        execvp("ls", args);
        perror("ls");
        exit(1);
    }
    waitpid(pid, NULL, 0);
    return 1;
}

int cmd_pwd(char **args) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("pwd");
    }
    return 1;
}

int cmd_cd(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "cd: missing argument\n");
    } else {
        if (chdir(args[1]) != 0) {
            perror("cd");
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
    pid_t pid = fork();
    if (pid == 0) {
        execvp("grep", args);
        perror("grep");
        exit(1);
    }
    waitpid(pid, NULL, 0);
    return 1;
}

// 명령어 파싱
void parse_command(char *cmd, char **args, int *background, 
                   char **input_file, char **output_file) {
    int i = 0;
    *background = 0;
    *input_file = NULL;
    *output_file = NULL;
    
    char *token = strtok(cmd, " \t\n");
    while (token != NULL) {
        if (strcmp(token, "&") == 0) {
            *background = 1;
        } else if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " \t\n");
            if (token) *input_file = token;
        } else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, " \t\n");
            if (token) *output_file = token;
        } else {
            args[i++] = token;
        }
        token = strtok(NULL, " \t\n");
    }
    args[i] = NULL;
}

// 파이프 처리
void execute_pipe(char *cmd) {
    char *commands[MAX_ARGS];
    int num_cmds = 0;
    
    // 파이프로 명령어 분리
    char *token = strtok(cmd, "|");
    while (token != NULL && num_cmds < MAX_ARGS) {
        commands[num_cmds++] = token;
        token = strtok(NULL, "|");
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
        
        pid_t pid = fork();
        if (pid == 0) {
            // 입력 재지향
            if (i > 0) {
                dup2(pipefds[(i - 1) * 2], STDIN_FILENO);
            }
            
            // 출력 재지향
            if (i < num_cmds - 1) {
                dup2(pipefds[i * 2 + 1], STDOUT_FILENO);
            }
            
            // 모든 파이프 닫기
            for (int j = 0; j < 2 * (num_cmds - 1); j++) {
                close(pipefds[j]);
            }
            
            execvp(args[0], args);
            perror("execvp");
            exit(1);
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
    
    // exit 명령어 처리
    if (strcmp(args[0], "exit") == 0) {
        return 0;
    }
    
    // 내장 명령어 처리
    if (strcmp(args[0], "cd") == 0) {
        return cmd_cd(args);
    } else if (strcmp(args[0], "pwd") == 0) {
        return cmd_pwd(args);
    } else if (strcmp(args[0], "ls") == 0) {
        return cmd_ls(args);
    } else if (strcmp(args[0], "mkdir") == 0) {
        return cmd_mkdir(args);
    } else if (strcmp(args[0], "rmdir") == 0) {
        return cmd_rmdir(args);
    } else if (strcmp(args[0], "ln") == 0) {
        return cmd_ln(args);
    } else if (strcmp(args[0], "cp") == 0) {
        return cmd_cp(args);
    } else if (strcmp(args[0], "rm") == 0) {
        return cmd_rm(args);
    } else if (strcmp(args[0], "mv") == 0) {
        return cmd_mv(args);
    } else if (strcmp(args[0], "cat") == 0) {
        return cmd_cat(args);
    } else if (strcmp(args[0], "grep") == 0) {
        return cmd_grep(args);
    }
    
    // 외부 명령어 실행
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
        
        execvp(args[0], args);
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
    
    printf("Simple Shell - Type 'exit' to quit\n");
    
    while (1) {
        printf("myshell> ");
        fflush(stdout);
        
        if (fgets(cmd, MAX_CMD_LEN, stdin) == NULL) {
            break;
        }
        
        // 빈 명령어 처리
        if (cmd[0] == '\n') {
            continue;
        }
        
        // 파이프 확인
        if (strchr(cmd, '|') != NULL) {
            cmd[strcspn(cmd, "\n")] = 0;
            execute_pipe(cmd);
            continue;
        }
        
        // 명령어 파싱
        parse_command(cmd, args, &background, &input_file, &output_file);
        
        // 명령어 실행
        if (!execute_command(args, background, input_file, output_file)) {
            break;
        }
    }
    
    printf("Goodbye!\n");
    return 0;
}
