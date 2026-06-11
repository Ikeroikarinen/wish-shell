#define _GNU_SOURCE

/*
 * wish.c - simple Unix shell for the OSTEP/LUT Unix Shell project.
 *
 * The implementation follows the project specification: interactive and batch
 * modes, built-in commands (exit, cd, path), external commands through execv(),
 * output/error redirection with '>', and parallel commands separated by '&'.
 *
 * External references used: the course assignment text and the standard Linux
 * manual pages for getline(3), fork(2), execv(3), waitpid(2), chdir(2),
 * access(2), open(2), dup2(2), and write(2).
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define INITIAL_PATH "/bin"
#define TOKEN_DELIMS " \t\r\n"

static char error_message[] = "An error has occurred\n";

static char **shell_paths = NULL;
static size_t path_count = 0;

static void print_error(void) {
    (void)write(STDERR_FILENO, error_message, strlen(error_message));
}

static char *trim_whitespace(char *text) {
    char *end;

    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text++;
    }

    if (*text == '\0') {
        return text;
    }

    end = text + strlen(text) - 1;
    while (end > text && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }

    return text;
}

static void free_paths(void) {
    for (size_t i = 0; i < path_count; i++) {
        free(shell_paths[i]);
    }
    free(shell_paths);
    shell_paths = NULL;
    path_count = 0;
}

static int set_paths(char **paths, size_t count) {
    char **new_paths = NULL;

    if (count > 0) {
        new_paths = malloc(count * sizeof(char *));
        if (new_paths == NULL) {
            print_error();
            return -1;
        }

        for (size_t i = 0; i < count; i++) {
            new_paths[i] = strdup(paths[i]);
            if (new_paths[i] == NULL) {
                for (size_t j = 0; j < i; j++) {
                    free(new_paths[j]);
                }
                free(new_paths);
                print_error();
                return -1;
            }
        }
    }

    free_paths();
    shell_paths = new_paths;
    path_count = count;
    return 0;
}

static int init_paths(void) {
    char *initial[] = {INITIAL_PATH};
    return set_paths(initial, 1);
}

static char **parse_arguments(char *command, int *argc_out) {
    int capacity = 8;
    int argc = 0;
    char **argv = malloc((size_t)capacity * sizeof(char *));
    char *token;
    char *saveptr = NULL;

    if (argv == NULL) {
        print_error();
        return NULL;
    }

    token = strtok_r(command, TOKEN_DELIMS, &saveptr);
    while (token != NULL) {
        if (argc + 1 >= capacity) {
            int new_capacity = capacity * 2;
            char **tmp = realloc(argv, (size_t)new_capacity * sizeof(char *));
            if (tmp == NULL) {
                free(argv);
                print_error();
                return NULL;
            }
            argv = tmp;
            capacity = new_capacity;
        }

        argv[argc] = token;
        argc++;
        token = strtok_r(NULL, TOKEN_DELIMS, &saveptr);
    }

    argv[argc] = NULL;
    *argc_out = argc;
    return argv;
}

static int parse_redirection(char *command, char **command_part, char **output_file) {
    char *first_redirect = strchr(command, '>');
    char *second_redirect;
    char *right_side;
    char *extra_token;
    char *saveptr = NULL;

    *command_part = command;
    *output_file = NULL;

    if (first_redirect == NULL) {
        return 0;
    }

    second_redirect = strchr(first_redirect + 1, '>');
    if (second_redirect != NULL) {
        return -1;
    }

    *first_redirect = '\0';
    right_side = trim_whitespace(first_redirect + 1);
    *command_part = trim_whitespace(command);

    if (**command_part == '\0' || *right_side == '\0') {
        return -1;
    }

    *output_file = strtok_r(right_side, TOKEN_DELIMS, &saveptr);
    if (*output_file == NULL) {
        return -1;
    }

    extra_token = strtok_r(NULL, TOKEN_DELIMS, &saveptr);
    if (extra_token != NULL) {
        return -1;
    }

    return 0;
}

static int handle_builtin(char **argv, int argc, bool *was_builtin) {
    *was_builtin = true;

    if (strcmp(argv[0], "exit") == 0) {
        if (argc != 1) {
            print_error();
            return -1;
        }
        free_paths();
        exit(0);
    }

    if (strcmp(argv[0], "cd") == 0) {
        if (argc != 2) {
            print_error();
            return -1;
        }
        if (chdir(argv[1]) != 0) {
            print_error();
            return -1;
        }
        return 0;
    }

    if (strcmp(argv[0], "path") == 0) {
        if (set_paths(&argv[1], (size_t)(argc - 1)) != 0) {
            return -1;
        }
        return 0;
    }

    *was_builtin = false;
    return 0;
}

static char *find_executable(const char *program_name) {
    for (size_t i = 0; i < path_count; i++) {
        size_t needed = strlen(shell_paths[i]) + strlen(program_name) + 2;
        char *candidate = malloc(needed);
        if (candidate == NULL) {
            print_error();
            return NULL;
        }

        snprintf(candidate, needed, "%s/%s", shell_paths[i], program_name);
        if (access(candidate, X_OK) == 0) {
            return candidate;
        }

        free(candidate);
    }

    return NULL;
}

static pid_t run_external(char **argv, const char *output_file) {
    char *executable = find_executable(argv[0]);
    pid_t pid;

    if (executable == NULL) {
        print_error();
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        free(executable);
        print_error();
        return -1;
    }

    if (pid == 0) {
        if (output_file != NULL) {
            int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (fd < 0) {
                print_error();
                _exit(1);
            }
            if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
                print_error();
                close(fd);
                _exit(1);
            }
            close(fd);
        }

        execv(executable, argv);
        print_error();
        _exit(1);
    }

    free(executable);
    return pid;
}

static int add_pid(pid_t **pids, size_t *pid_count, size_t *pid_capacity, pid_t pid) {
    if (*pid_count >= *pid_capacity) {
        size_t new_capacity = (*pid_capacity == 0) ? 4 : (*pid_capacity * 2);
        pid_t *tmp = realloc(*pids, new_capacity * sizeof(pid_t));
        if (tmp == NULL) {
            print_error();
            return -1;
        }
        *pids = tmp;
        *pid_capacity = new_capacity;
    }

    (*pids)[*pid_count] = pid;
    (*pid_count)++;
    return 0;
}

static void wait_for_children(pid_t *pids, size_t pid_count) {
    for (size_t i = 0; i < pid_count; i++) {
        while (waitpid(pids[i], NULL, 0) < 0) {
            print_error();
            break;
        }
    }
}

static void execute_single_command(char *raw_command, pid_t **pids, size_t *pid_count, size_t *pid_capacity) {
    char *command = trim_whitespace(raw_command);
    char *command_part = NULL;
    char *output_file = NULL;
    char **argv = NULL;
    int argc = 0;
    bool was_builtin = false;
    pid_t pid;

    if (*command == '\0') {
        return;
    }

    if (parse_redirection(command, &command_part, &output_file) != 0) {
        print_error();
        return;
    }

    argv = parse_arguments(command_part, &argc);
    if (argv == NULL) {
        return;
    }

    if (argc == 0) {
        free(argv);
        return;
    }

    if (handle_builtin(argv, argc, &was_builtin) != 0) {
        free(argv);
        return;
    }

    if (was_builtin) {
        free(argv);
        return;
    }

    pid = run_external(argv, output_file);
    if (pid > 0) {
        (void)add_pid(pids, pid_count, pid_capacity, pid);
    }

    free(argv);
}

static void execute_line(char *line) {
    char *command;
    char *saveptr = NULL;
    pid_t *pids = NULL;
    size_t pid_count = 0;
    size_t pid_capacity = 0;

    command = strtok_r(line, "&", &saveptr);
    while (command != NULL) {
        execute_single_command(command, &pids, &pid_count, &pid_capacity);
        command = strtok_r(NULL, "&", &saveptr);
    }

    wait_for_children(pids, pid_count);
    free(pids);
}

int main(int argc, char *argv[]) {
    FILE *input = stdin;
    bool interactive = true;
    char *line = NULL;
    size_t line_capacity = 0;

    if (argc > 2) {
        print_error();
        exit(1);
    }

    if (argc == 2) {
        input = fopen(argv[1], "r");
        if (input == NULL) {
            print_error();
            exit(1);
        }
        interactive = false;
    }

    if (init_paths() != 0) {
        if (input != stdin) {
            fclose(input);
        }
        exit(1);
    }

    while (true) {
        if (interactive) {
            printf("wish> ");
            fflush(stdout);
        }

        if (getline(&line, &line_capacity, input) == -1) {
            break;
        }

        execute_line(line);
    }

    free(line);
    free_paths();
    if (input != stdin) {
        fclose(input);
    }

    return 0;
}
