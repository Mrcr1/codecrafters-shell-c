#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);

    while (1)
    {
        printf("$ ");

        char command[1024];
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = '\0';

        // Split on the first space only
        char *space = strchr(command, ' ');
        char *builtin = command;
        char *arg = NULL;

        if (space != NULL)
        {
            *space = '\0';   // terminate builtin name
            arg = space + 1; // arg = everything after the first space
        }

        // Skip empty input
        if (*builtin == '\0')
            continue;

        if (strcmp(builtin, "exit") == 0)
        {
            break;
        }
        else if (strcmp(builtin, "echo") == 0)
        {
            printf("%s\n", arg ? arg : "");
        }
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
            else
            {
                printf("%s: not found\n", arg);
            }
        }
        else
        {
            printf("%s: command not found\n", builtin);
        }
    }

    return 0;
}