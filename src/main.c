#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
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
                    if (*p == '\\' && (*(p+1) == '"' || *(p+1) == '\\'))
                    {
                        p++;
                        buf[buf_len++] = *p++;
                    }
                    else
                        buf[buf_len++] = *p++;
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
        if (strcmp(args[i], "2>>") == 0 )
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
            continue;

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

// Builtins list for tab completion
char *builtin_list[] = { "echo", "exit", "type", "pwd", "cd", NULL };

char *builtin_generator(const char *text, int state)
{
    static int index;
    static int len;

    if (state == 0)
    {
        index = 0;
        len = strlen(text);
    }

    while (builtin_list[index] != NULL)
    {
        char *name = builtin_list[index++];
        if (strncmp(name, text, len) == 0)
            return strdup(name);
    }

    return NULL;
}

char **shell_completion(const char *text, int start, int end)
{
    (void)end;
    rl_attempted_completion_over = 1;  // disable default filename completion
    if (start == 0)
        return rl_completion_matches(text, builtin_generator);
    return NULL;
}

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);

    // Set up tab completion
    rl_attempted_completion_function = shell_completion;

    while (1)
    {
        char *command = readline("$ ");
        if (command == NULL) break;  // Ctrl+D

        // Skip empty input
        if (command[0] == '\0')
        {
            free(command);
            continue;
        }

        char *args[1024];
        int nargs = parse_args(command, args, 1024);
        free(command);

        if (nargs == 0) continue;

        char *builtin = args[0];

        // Check for redirection
        int is_stderr = 0;
        int is_append = 0;
        char *outfile = extract_redirect(args, &nargs, &is_stderr, &is_append);

        // Save and redirect the right fd
        int saved_fd = -1;
        int target_fd = is_stderr ? STDERR_FILENO : STDOUT_FILENO;

        if (outfile != NULL)
        {
            saved_fd = dup(target_fd);
            int flags = O_WRONLY | O_CREAT | (is_append ? O_APPEND : O_TRUNC);
            int fd = open(outfile, flags, 0644);
            if (fd < 0)
            {
                perror("open");
                free(outfile);
                free_args(args, nargs);
                continue;
            }
            dup2(fd, target_fd);
            close(fd);
        }

        // Exit builtin:
        if (strcmp(builtin, "exit") == 0)
        {
            if (outfile) { dup2(saved_fd, target_fd); close(saved_fd); free(outfile); }
            free_args(args, nargs);
            break;
        }

        // Echo builtin:
        else if (strcmp(builtin, "echo") == 0)
        {
            for (int i = 1; i < nargs; i++)
            {
                if (i > 1) printf(" ");
                printf("%s", args[i]);
            }
            printf("\n");
        }

        // pwd builtin:
        else if (strcmp(builtin, "pwd") == 0)
        {
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd)) != NULL)
                printf("%s\n", cwd);
            else
                perror("pwd");
        }

        // cd builtin:
        else if (strcmp(builtin, "cd") == 0)
        {
            if (nargs < 2)
            {
                printf("cd: missing argument\n");
            }
            else
            {
                char *path = args[1];
                if (strcmp(path, "~") == 0)
                {
                    path = getenv("HOME");
                    if (path == NULL)
                    {
                        printf("cd: HOME not set\n");
                        if (outfile) { dup2(saved_fd, target_fd); close(saved_fd); free(outfile); }
                        free_args(args, nargs);
                        continue;
                    }
                }
                if (chdir(path) != 0)
                    printf("cd: %s: No such file or directory\n", args[1]);
            }
        }

        // type builtin:
        else if (strcmp(builtin, "type") == 0)
        {
            if (nargs < 2)
            {
                printf("type: missing argument\n");
            }
            else
            {
                char *name = args[1];
                if (!strcmp(name, "exit") || !strcmp(name, "echo") ||
                    !strcmp(name, "type") || !strcmp(name, "pwd") ||
                    !strcmp(name, "cd"))
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

        // External program execution:
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
                else if (pid > 0)
                {
                    waitpid(pid, NULL, 0);
                }
                else
                {
                    perror("fork");
                }

                free(full_path);
            }
        }

        // Restore fd
        if (outfile != NULL)
        {
            fflush(stdout);
            dup2(saved_fd, target_fd);
            close(saved_fd);
            free(outfile);
        }

        free_args(args, nargs);
    }

    return 0;
}