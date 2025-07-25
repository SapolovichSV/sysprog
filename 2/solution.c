#include "parser.h"
#include "sys/types.h"

#define DEBUG 0 // 1 - включить отладку, 0 - выключить

#if DEBUG
#define DBG_PRINT(fmt, ...)                                                    \
    printf("[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define DBG_PRINT(fmt, ...)
#endif
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
void DEBUG__print_line(const struct command_line *l) {
    DBG_PRINT("out_type : %d \n", l->out_type);
    DBG_PRINT("output_type: %s", l->out_file);
    return (void)l->head;
    // continue
}
void DEBUG__print_args(char **args) {
    DBG_PRINT("args : ");
    for (uint i = 0; args[i] != NULL; i++) {
        DBG_PRINT("%s ", args[i]);
    }
    DBG_PRINT("\n");
}

bool exec_exit(struct expr *exp, int *out_status_code) {
    struct expr *cur = exp;
    bool is_last = (cur->next == NULL);

    if (is_last) {
        *out_status_code = atoi(cur->cmd.args[0]);
        DBG_PRINT("exec_exit with code %d", *out_status_code);
        return true;
    }
    return false;
}
bool is_line_empty(const struct command_line *line) {
    if (line == NULL) {
        return true;
    } else {
        return false;
    }
}
int exec_cd(struct command cmd) {
    char *target_dir = NULL;

    DBG_PRINT("executing cd with args count: %d", cmd.arg_count);

    if (cmd.arg_count == 0) {
        // CD без аргументов - переход в HOME
        target_dir = getenv("HOME");
        if (target_dir == NULL) {
            DBG_PRINT("NO $HOME");
            return -1;
        }
    } else {
        target_dir = cmd.args[0];
    }

    if (chdir(target_dir) == -1) {
        perror("cd");
        return -1;
    }

    return 0;
}
char **build_args(const struct command cmd) {
    char **args;
    if (cmd.arg_count == 0) {
        args = calloc(2, sizeof(char *));
        args[0] = cmd.exe;
        args[1] = NULL;
    } else {
        DBG_PRINT("DEBUG PRINT args: %s \n", *cmd.args);
        args = calloc(cmd.arg_count + 2, sizeof(char *));
        args[0] = cmd.exe;
        memcpy(&args[1], cmd.args, sizeof(char *) * cmd.arg_count);
        args[cmd.arg_count + 1] = NULL;
    }
    DEBUG__print_args(args);
    return args;
}
// must fork before use
int execute_command(struct command cmd, int *last_status) {
    DBG_PRINT("executing command %s", cmd.exe);
    if (strcmp(cmd.exe, "echo") == 0) {
        if (strcmp(cmd.args[0], "$?") == 0) {
            printf("%d", *last_status);
            return 0;
        }
    }
    char **args = build_args(cmd);
    execvp(args[0], args);
    perror("error at execvp \n");
    return -1;
}
static void execute_command_line(const struct command_line *line,
                                 int *exit_code) {
    if (is_line_empty(line)) {
        DBG_PRINT("line is empty");
        exit(0);
    }
    DEBUG__print_line(line);

    struct expr *exp = line->head;
    int prev_pipe_read = -1;
    pid_t last_pid = -1;
    int output_fd = -1;
    int flags = 0;
    int saved_stdout = -1;
    int exit_status = -1;
    switch (line->out_type) {
    case OUTPUT_TYPE_STDOUT:
        break;
    case OUTPUT_TYPE_FILE_NEW: {
        saved_stdout = dup(STDOUT_FILENO);
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        output_fd = open(line->out_file, flags, 0644);
        break;
    }
    case OUTPUT_TYPE_FILE_APPEND: {
        saved_stdout = dup(STDOUT_FILENO);
        flags = O_WRONLY | O_CREAT | O_APPEND;
        output_fd = open(line->out_file, flags, 0644);
        break;
    }
    }
    int *last_status = exit_code;

    while (exp != NULL) {

        DBG_PRINT("in switch with exp type: %u", exp->type);

        switch (exp->type) {
        case EXPR_TYPE_COMMAND: {
            if (strcmp(exp->cmd.exe, "cd") == 0) {
                exec_cd(exp->cmd);
                exp = exp->next;
                break;
            }
            if (strcmp(exp->cmd.exe, "exit") == 0) {
                bool success_exit = exec_exit(exp, &exit_status);
                exp = exp->next;
                DBG_PRINT("succes_exit = %d", success_exit);
                if (success_exit) {
                    *last_status = exit_status;
                    goto break_cycle;
                }
            }
            bool is_last = (exp->next == NULL);
            int pipes[2];

            if (!is_last && exp->next->type == EXPR_TYPE_PIPE) {
                if (pipe(pipes) == -1) {
                    DBG_PRINT("can't pipe()");
                    exit(1);
                }
            }

            pid_t pid = fork();
            if (pid == 0) {
                if (prev_pipe_read != -1) {
                    dup2(prev_pipe_read, STDIN_FILENO);
                    close(prev_pipe_read);
                }
                if (is_last && output_fd != -1) {
                    dup2(output_fd, STDOUT_FILENO);
                    close(output_fd);
                    output_fd = -1;
                } else if (!is_last && exp->next->type == EXPR_TYPE_PIPE) {
                    close(pipes[0]);
                    dup2(pipes[1], STDOUT_FILENO);
                    close(pipes[1]);
                }

                DBG_PRINT("forked cmd: %s", exp->cmd.exe);
                execute_command(exp->cmd, last_status);
            }

            if (prev_pipe_read != -1) {
                close(prev_pipe_read);
            }

            // Обновляем pipe только если следующее выражение пайп
            if (!is_last && exp->next->type == EXPR_TYPE_PIPE) {
                close(pipes[1]);
                prev_pipe_read = pipes[0];
            } else {
                prev_pipe_read = -1;
            }

            last_pid = pid;
            exp = exp->next;
            break;
        }
        case EXPR_TYPE_PIPE:
            exp = exp->next;
            break;

        case EXPR_TYPE_OR:
            waitpid(last_pid, last_status, 0);

            if (WEXITSTATUS(*last_status) == 0) {
                while (exp != NULL && exp->type == EXPR_TYPE_OR) {
                    exp = exp->next;
                    if (exp != NULL && exp->type == EXPR_TYPE_COMMAND) {
                        exp = exp->next;
                    }
                }
            } else {
                exp = exp->next;
            }
            break;
        case EXPR_TYPE_AND: {
            waitpid(last_pid, last_status, 0);
            if (WEXITSTATUS(*last_status) != 0) {
                while (exp != NULL && exp->type == EXPR_TYPE_AND) {
                    exp = exp->next;
                    if (exp != NULL && exp->type == EXPR_TYPE_COMMAND) {
                        exp = exp->next;
                    }
                }
            } else {
                exp = exp->next;
            }
            break;
        }
        default:
            DBG_PRINT("impossible default");
        }
    }
break_cycle:
    if (saved_stdout != -1) {
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
    }

    if (last_pid > 0 && exit_status == -1) {
        int status;
        waitpid(last_pid, &status, 0);
        *last_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
    while (wait(NULL) > 0) {
    }

    if (output_fd != -1) {
        close(output_fd);
    }
}

int main(void) {
    const size_t buf_size = 1024;
    char buf[buf_size];
    int rc;
    int exit_code = 0;
    struct parser *p = parser_new();
    while ((rc = read(STDIN_FILENO, buf, buf_size)) > 0) {
        parser_feed(p, buf, rc);
        struct command_line *line = NULL;
        while (true) {
            enum parser_error err = parser_pop_next(p, &line);
            if (err == PARSER_ERR_NONE && line == NULL)
                break;
            if (err != PARSER_ERR_NONE) {
                printf("Error: %d\n", (int)err);
                continue;
            }
            if (line->head && line->head->type == EXPR_TYPE_COMMAND) {
                struct expr *next = line->head->next;
                struct command *cmd = &line->head->cmd;
                if (strcmp(cmd->exe, "exit") == 0 &&
                    (!next || next->type != EXPR_TYPE_PIPE)) {
                    exit_code = cmd->arg_count ? atoi(cmd->args[0]) : 0;
                    DBG_PRINT("catch final exit with code: %d", exit_code);
                    command_line_delete(line);
                    goto cleanup;
                }
            }
            execute_command_line(line, &exit_code);
            command_line_delete(line);
        }
    }
cleanup:
    parser_delete(p);
    return exit_code;
}
