#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

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

// Parses input into args array, handling single quotes
int parse_args(char *input, char **args, int max_args)
{
    int argc = 0;
    char *p = input;
    char buf[1024];

    while (*p != '\0' && argc < max_args - 1)
    {
        // Skip spaces between arguments
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        int buf_len = 0;

        // Build one argument
        while (*p != '\0')
        {
            if (*p == '\'')
            {
                // Inside single quotes — copy literally
                p++; // skip opening '
                while (*p != '\0' && *p != '\'')
                {
                    buf[buf_len++] = *p++;
                }
                if (*p == '\'') p++; // skip closing '
            }

            else if (*p == '"')
            {
                // Inside double quotes — copy literally
                p++; // skip opening "
                while (*p != '\0' && *p != '"')
                {
                    buf[buf_len++] = *p++;
                }
                if (*p == '"') p++; // skip closing "
            }

            else if (*p == '\\')
            {
                // Backlash Outside Options - escape next character
                p++; // skip opening "
                if (*p != '\0')
                {
                    buf[buf_len++] = *p++; // Take the next character literally
                }
            }

            else if (*p == ' ' || *p == '\t')
            {
                // Unquoted space = end of argument
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

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);

    while (1)
    {
        printf("$ ");

        char command[1024];
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = '\0';

        // Parse into args array
        char *args[1024];
        int nargs = parse_args(command, args, 1024);

        if (nargs == 0) continue;

        char *builtin = args[0];

        // Exit builtin:
        if (strcmp(builtin, "exit") == 0)
        {
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

        free_args(args, nargs);
    }

    return 0;
}