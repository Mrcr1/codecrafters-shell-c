#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);

    while (1)
    {
        printf("$ ");

        // Read the entire command line input
        char command[1024];
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = '\0';

        // Split on the first space only to separate the builtin command from its argument
        char *space = strchr(command, ' ');
        char *builtin = command;
        char *arg = NULL;

        // If there is a space, terminate the builtin command and set arg to the rest of the string
        if (space != NULL)
        {
            *space = '\0';   // terminate builtin name
            arg = space + 1; // arg = everything after the first space
        }

        // Builtin Block Starts
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
        
        // type builtin:
        else if (strcmp(builtin, "type") == 0)
        {
            if (arg == NULL)
            {
                printf("type: missing argument\n");
            }
            else if (!strcmp(arg, "exit") || !strcmp(arg, "echo") || !strcmp(arg, "type"))
            {
                printf("%s is a shell builtin\n", arg);
            }
                        
            // type executables:
            else
            {
                char *path_env = getenv("PATH");
                int found = 0;
                
                if (path_env != NULL)
                {
                    //Duplicate the PATH environment variable to avoid modifying the original
                    char *path_copy = strdup(path_env);
                    char *dir = strtok(path_copy, ":");

                    while (dir != NULL)
                    {
                        char full_path[1024];
                        snprintf(full_path, sizeof(full_path), "%s/%s", dir, arg);

                        if (access(full_path, X_OK) == 0)
                        {
                            printf("%s is %s\n", arg, full_path);
                            found = 1;
                            break;
                        }

                        dir = strtok(NULL, ":");
                    }

                    free(path_copy);
                    
                }

                if (!found)
                {
                    printf("%s not found\n", arg);
                }
                
            }
        }
        else
        {
            printf("%s: command not found\n", builtin);
        }
    }

    return 0;
}