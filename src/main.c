#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
  // Flush after every printf
  setbuf(stdout, NULL);

  // TODO: Uncomment the code below to pass the first stage
  while(1) 
  {
   printf("$ ");

   //Captures User's command in Command Variable;
    char command[1024];
    fgets(command, sizeof(command), stdin);
  
    //Remove Trailing Newline
    command[strcspn(command, "\n")] = '\0';

    char *builtin = strtok(command, " ");
    char *arg = strtok(NULL, " ");
    if (builtin == NULL) 
    {
      continue; 
    }



    if (strcmp(builtin, "exit") ==0)
    {
      break;
    }

    else if (strncmp(builtin, "echo", 4) == 0)
    {
      printf("%s\n", arg);
    }
    else if (strcmp(builtin, "type") == 0)
    {
      if (!strcmp(arg, "exit") || !strcmp(arg, "echo") || !strcmp(arg, "type"))
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
    // Prints the "<command>: command not found" message
    printf("%s: command not found\n", builtin);
    }
    
  }

  return 0;
}
