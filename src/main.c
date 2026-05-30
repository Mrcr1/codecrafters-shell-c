#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <readline/readline.h>
#include <readline/history.h>

char *find_in_path(const char *cmd)
{
    char *path_env = getenv("PATH");
    if (path_env == NULL) return NULL;

    char *path_copy = strdup(path_env);
    char *dir = strtok(path_copy, ":");

    while (dir != NULL)
    {
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, cmd);

        if (access(full_path, X_OK) == 0)
        {
            free(path_copy);
            return strdup(full_path);
        }

        dir = strtok(NULL, ":");
    }

    free(path_copy);
    return NULL;
}

int parse_args(char *input, char **args, int max_args)
{
    int argc = 0;
    char *p = input;
    char buf[1024];

    while (*p != '\0' && argc < max_args - 1)
    {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        int buf_len = 0;

        while (*p != '\0')
        {
            if (*p == '\'')
            {
                p++;
                while (*p != '\0' && *p != '\'')
                    buf[buf_len++] = *p++;
                if (*p == '\'') p++;
            }
            else if (*p == '"')
            {
                p++;
                while (*p != '\0' && *p != '"')
                {
                    if (*p == '\\' && (*(p + 1) == '"' || *(p + 1) == '\\'))
                    {
                        p++;
                        buf[buf_len++] = *p++;
                    }
                    else
                    {
                        buf[buf_len++] = *p++;
                    }
                }

                if (*p == '"') p++;
            }
            else if (*p == '\\')
            {
                p++;
                if (*p != '\0')
                    buf[buf_len++] = *p++;
            }
            else if (*p == ' ' || *p == '\t')
            {
                break;
            }
            else
            {
                buf[buf_len++] = *p++;
            }
        }

        buf[buf_len] = '\0';
        args[argc++] = strdup(buf);
    }

    args[argc] = NULL;
    return argc;
}

void free_args(char **args, int argc)
{
    for (int i = 0; i < argc; i++)
        free(args[i]);
}

char *extract_redirect(char **args, int *nargs, int *is_stderr, int *is_append)
{
    for (int i = 0; i < *nargs; i++)
    {
        if (strcmp(args[i], "2>>") == 0)
        {
            *is_stderr = 1;
            *is_append = 1;
        }
        else if (strcmp(args[i], "2>") == 0)
        {
            *is_stderr = 1;
            *is_append = 0;
        }
        else if (strcmp(args[i], "1>>") == 0 || strcmp(args[i], ">>") == 0)
        {
            *is_stderr = 0;
            *is_append = 1;
        }
        else if (strcmp(args[i], "1>") == 0 || strcmp(args[i], ">") == 0)
        {
            *is_stderr = 0;
            *is_append = 0;
        }
        else
        {
            continue;
        }

        if (i + 1 < *nargs)
        {
            char *file = args[i + 1];

            free(args[i]);

            for (int j = i; j < *nargs - 2; j++)
                args[j] = args[j + 2];

            *nargs -= 2;
            args[*nargs] = NULL;

            return file;
        }
    }

    return NULL;
}

int find_pipe(char **args, int nargs)
{
    for (int i = 0; i < nargs; i++)
    {
        if (strcmp(args[i], "|") == 0)
            return i;
    }

    return -1;
}

int is_builtin(char *cmd)
{
    return !strcmp(cmd, "exit") ||
           !strcmp(cmd, "echo") ||
           !strcmp(cmd, "type") ||
           !strcmp(cmd, "pwd") ||
           !strcmp(cmd, "cd") ||
           !strcmp(cmd, "complete") ||
           !strcmp(cmd, "jobs");
}

void execute_builtin(char **args, int nargs)
{
    char *builtin = args[0];

    if (strcmp(builtin, "echo") == 0)
    {
        for (int i = 1; i < nargs; i++)
        {
            if (i > 1) printf(" ");
            printf("%s", args[i]);
        }

        printf("\n");
    }

    else if (strcmp(builtin, "pwd") == 0)
    {
        char cwd[1024];

        if (getcwd(cwd, sizeof(cwd)) != NULL)
            printf("%s\n", cwd);
        else
            perror("pwd");
    }

    else if (strcmp(builtin, "type") == 0)
    {
        if (nargs < 2)
        {
            printf("type: missing argument\n");
        }
        else
        {
            char *name = args[1];

            if (is_builtin(name))
            {
                printf("%s is a shell builtin\n", name);
            }
            else
            {
                char *full_path = find_in_path(name);

                if (full_path != NULL)
                {
                    printf("%s is %s\n", name, full_path);
                    free(full_path);
                }
                else
                {
                    printf("%s: not found\n", name);
                }
            }
        }
    }
}

#define MAX_COMPLETIONS 256

typedef struct
{
    char *command;
    char *script;
} Completion;

Completion completions[MAX_COMPLETIONS];
int completion_count = 0;

#define MAX_JOBS 256

typedef struct
{
    int job_num;
    pid_t pid;
    char *cmd;
    char *status;
} Job;

Job jobs_list[MAX_JOBS];
int jobs_count = 0;

char *builtin_list[] = {"echo", "exit", "type", "pwd", "cd", "complete", "jobs", NULL};

char **matches = NULL;
int match_count = 0;

char *builtin_generator(const char *text, int state)
{
    static int index;
    static int len;

    if (state == 0)
    {
        index = 0;
        len = strlen(text);
        match_count = 0;

        if (matches != NULL)
        {
            for (int i = 0; matches[i] != NULL; i++)
                free(matches[i]);

            free(matches);
            matches = NULL;
        }

        matches = malloc(sizeof(char *) * 1024);
        matches[0] = NULL;

        for (int i = 0; builtin_list[i] != NULL; i++)
        {
            if (strncmp(builtin_list[i], text, len) == 0)
                matches[match_count++] = strdup(builtin_list[i]);
        }

        char *path_env = getenv("PATH");

        if (path_env != NULL)
        {
            char *path_copy = strdup(path_env);
            char *dir = strtok(path_copy, ":");

            while (dir != NULL)
            {
                DIR *dp = opendir(dir);

                if (dp != NULL)
                {
                    struct dirent *entry;

                    while ((entry = readdir(dp)) != NULL)
                    {
                        if (strncmp(entry->d_name, text, len) == 0)
                        {
                            char full_path[1024];

                            snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name);

                            if (access(full_path, X_OK) == 0)
                            {
                                int dup = 0;

                                for (int i = 0; i < match_count; i++)
                                {
                                    if (strcmp(matches[i], entry->d_name) == 0)
                                    {
                                        dup = 1;
                                        break;
                                    }
                                }

                                if (!dup && match_count < 1023)
                                    matches[match_count++] = strdup(entry->d_name);
                            }
                        }
                    }

                    closedir(dp);
                }

                dir = strtok(NULL, ":");
            }

            free(path_copy);
        }

        matches[match_count] = NULL;
    }

    if (index < match_count)
        return strdup(matches[index++]);

    return NULL;
}

char **shell_completion(const char *text, int start, int end)
{
    (void)end;

    if (start == 0)
    {
        rl_attempted_completion_over = 1;
        return rl_completion_matches(text, builtin_generator);
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);

    rl_attempted_completion_function = shell_completion;

    while (1)
    {
        char *command = readline("$ ");

        if (command == NULL)
            break;

        if (command[0] == '\0')
        {
            free(command);
            continue;
        }

        char *args[1024];

        int nargs = parse_args(command, args, 1024);

        free(command);

        if (nargs == 0)
            continue;

        int pipe_index = find_pipe(args, nargs);

        if (pipe_index != -1)
        {
            args[pipe_index] = NULL;

            char **left_args = args;
            char **right_args = &args[pipe_index + 1];

            int left_argc = pipe_index;
            int right_argc = nargs - pipe_index - 1;

            int pipefd[2];

            if (pipe(pipefd) < 0)
            {
                perror("pipe");
                free_args(args, nargs);
                continue;
            }

            pid_t pid1 = fork();

            if (pid1 == 0)
            {
                dup2(pipefd[1], STDOUT_FILENO);

                close(pipefd[0]);
                close(pipefd[1]);

                if (is_builtin(left_args[0]))
                {
                    execute_builtin(left_args, left_argc);
                    exit(0);
                }

                char *left_path = find_in_path(left_args[0]);

                if (left_path == NULL)
                {
                    printf("%s: command not found\n", left_args[0]);
                    exit(1);
                }

                execv(left_path, left_args);

                perror("execv");
                exit(1);
            }

            pid_t pid2 = fork();

            if (pid2 == 0)
            {
                dup2(pipefd[0], STDIN_FILENO);

                close(pipefd[1]);
                close(pipefd[0]);

                if (is_builtin(right_args[0]))
                {
                    execute_builtin(right_args, right_argc);
                    exit(0);
                }

                char *right_path = find_in_path(right_args[0]);

                if (right_path == NULL)
                {
                    printf("%s: command not found\n", right_args[0]);
                    exit(1);
                }

                execv(right_path, right_args);

                perror("execv");
                exit(1);
            }

            close(pipefd[0]);
            close(pipefd[1]);

            waitpid(pid1, NULL, 0);
            waitpid(pid2, NULL, 0);

            free_args(args, nargs);

            continue;
        }

        char *builtin = args[0];

        if (strcmp(builtin, "exit") == 0)
        {
            free_args(args, nargs);
            break;
        }

        else if (is_builtin(builtin))
        {
            execute_builtin(args, nargs);
        }

        else
        {
            char *full_path = find_in_path(builtin);

            if (full_path == NULL)
            {
                printf("%s: command not found\n", builtin);
            }
            else
            {
                pid_t pid = fork();

                if (pid == 0)
                {
                    execv(full_path, args);

                    perror("execv");
                    exit(1);
                }
                else
                {
                    waitpid(pid, NULL, 0);
                }

                free(full_path);
            }
        }

        free_args(args, nargs);
    }

    return 0;
}