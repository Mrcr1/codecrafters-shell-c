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

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);

    while (1)
    {
        printf("$ ");

        char command[1024];
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = '\0';

        char *space = strchr(command, ' ');
        char *builtin = command;
        char *arg = NULL;

        if (space != NULL)
        {
            *space = '\0';
            arg = space + 1;
        }

        if (*builtin == '\0')
            continue;

        // Exit builtin:
        if (strcmp(builtin, "exit") == 0)
        {
            break;
        }

        // Echo builtin:
        else if (strcmp(builtin, "echo") == 0)
        {
            printf("%s\n", arg ? arg : "");
        }

        // pwd builtin:
        else if (strcmp(builtin, "pwd") == 0)
        {
            char cwd[1024];
            if (getcwd(cwd, sizeof(cwd)) != NULL)
            {
                printf("%s\n", cwd);
            }
            else
            {
                perror("pwd");
            }
        }

        // type builtin:
        else if (strcmp(builtin, "type") == 0)
        {
            if (arg == NULL)
            {
                printf("type: missing argument\n");
            }
            else if (!strcmp(arg, "exit") || !strcmp(arg, "echo") || !strcmp(arg, "type") || !strcmp(arg, "pwd"))
            {
                printf("%s is a shell builtin\n", arg);
            }
            else
            {
                char *full_path = find_in_path(arg);
                if (full_path != NULL)
                {
                    printf("%s is %s\n", arg, full_path);
                    free(full_path);
                }
                else
                {
                    printf("%s: not found\n", arg);
                }
            }
        }
        else
        {
            // External program execution:
            char *full_path = find_in_path(builtin);

            if (full_path == NULL)
            {
                printf("%s: command not found\n", builtin);
            }
            else
            {
                char *exec_args[1024];
                exec_args[0] = builtin;

                int i = 1;
                if (arg != NULL)
                {
                    char *token = strtok(arg, " ");
                    while (token != NULL && i < 1023)
                    {
                        exec_args[i++] = token;
                        token = strtok(NULL, " ");
                    }
                }
                exec_args[i] = NULL;

                pid_t pid = fork();

                if (pid == 0)
                {
                    execv(full_path, exec_args);
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
    }

    return 0;
}