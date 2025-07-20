#include "parser.h"
#include "sys/types.h"

#define DEBUG 1 // 1 - включить отладку, 0 - выключить

#if DEBUG
#define DBG_PRINT(fmt, ...)                                                    \
  printf("[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define DBG_PRINT(fmt, ...)
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void DEBUG__print_args(char **args) {
  DBG_PRINT("args : ");
  for (uint i = 0; args[i] != NULL; i++) {
    DBG_PRINT("%s ", args[i]);
  }
  DBG_PRINT("\n");
}
bool is_line_empty(const struct command_line *line) {
  if (line == NULL) {
    return true;
  } else {
    return false;
  }
}
int command_cd(const char *current_dir, const char *to_dir) {
  char *new_path =
      malloc(sizeof(char) * (strlen(current_dir) + strlen(to_dir) + 1 +
                             1)); //+1 to null terminator and +1 to "/"
  new_path = strcpy(new_path, current_dir);
  new_path = strcat(new_path, "/");
  new_path = strcat(new_path, to_dir);
  int res = chdir(new_path);
  if (res) {
    DBG_PRINT("cd error: %d", res);
  }
  return res;
}
int exec_cd(struct command cmd) {
  char *to_dir = NULL;
  if (cmd.arg_count == 0) {
    to_dir = malloc(sizeof(char));
    *to_dir = '\0';
    char *current_dir = getenv("HOME");
    if (current_dir == NULL) {
      DBG_PRINT("NO $HOME");
      exit(1);
    }
    DBG_PRINT("start command_cd() without args");
    return command_cd(current_dir, to_dir);
  } else {
    DBG_PRINT("start command_cd()");
    char *current_dir = getcwd(NULL, _PC_PATH_MAX);
    return command_cd(current_dir, cmd.args[0]);
  }
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
int execute_command(struct command cmd) {
  if (strcmp(cmd.exe, "cd") == 0) {
    return exec_cd(cmd);
  }
  char **args = build_args(cmd);
  execvp(args[0], args);
  perror("error at execvp \n");
  return -1;
}
static void execute_command_line(const struct command_line *line) {
  if (is_line_empty(line)) {
    DBG_PRINT("line is empty");
    exit(0);
  }
  const struct expr *exp = line->head;
  const struct command cmd = exp->cmd;
  execute_command(cmd);
  //   int status = fork_and_execute_command(e->cmd);
  //   if (status != 0) {
  //     exit(status);
  //   }

  // e = e->next;
  // int pipes[2];
  // if (pipe(pipes) == -1)
  //   DBG_PRINT("error when open pipe");
  // if (fork() == 0) {
  //   close(pipes[0]);
  //   dup2(pipes[1], STDOUT_FILENO);
  //   execute_command(cmd);
  // }
  // e = e->next;
  // const struct command sec_cmd = e->cmd;
  // if (fork() == 0) {
  //   close(pipes[1]);
  //   dup2(pipes[0], STDIN_FILENO);
  //   execute_command(sec_cmd);
  // }
  // close(pipes[0]);
  // close(pipes[1]);
  // wait(NULL);
  // wait(NULL);
  // exit(0);
  /* REPLACE THIS CODE WITH ACTUAL COMMAND EXECUTION */

  assert(line != NULL);
  DBG_PRINT("================================\n");
  DBG_PRINT("Command line:\n");
  DBG_PRINT("Is background: %d\n", (int)line->is_background);
  DBG_PRINT("Output: ");
  if (line->out_type == OUTPUT_TYPE_STDOUT) {
    DBG_PRINT("stdout\n");
  } else if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
    DBG_PRINT("new file - \"%s\"\n", line->out_file);
  } else if (line->out_type == OUTPUT_TYPE_FILE_APPEND) {
    DBG_PRINT("append file - \"%s\"\n", line->out_file);
  } else {
    assert(false);
  }
  DBG_PRINT("Expressions:\n");
  const struct expr *e = line->head; // rename e2 to e
  while (e != NULL) {
    if (e->type == EXPR_TYPE_COMMAND) {
      DBG_PRINT("\tCommand: %s", e->cmd.exe);
      for (uint32_t i = 0; i < e->cmd.arg_count; ++i)
        DBG_PRINT(" %s", e->cmd.args[i]);
      DBG_PRINT("\n");
    } else if (e->type == EXPR_TYPE_PIPE) {
      DBG_PRINT("\tPIPE\n");
    } else if (e->type == EXPR_TYPE_AND) {
      DBG_PRINT("\tAND\n");
    } else if (e->type == EXPR_TYPE_OR) {
      DBG_PRINT("\tOR\n");
    } else {
      assert(false);
    }
    e = e->next;
  }
}

int main(void) {
  const size_t buf_size = 1024;
  char buf[buf_size];
  int rc;
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
      execute_command_line(line);
      command_line_delete(line);
    }
  }
  parser_delete(p);
  return 0;
}
