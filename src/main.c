#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  // TODO: Uncomment the code below to pass the first stage
  while(1) {
   printf("$ ");

   //Captures User's command in Command Variable;
    char command[1024];
    fgets(command, sizeof(command), stdin);
  
    //Remove Trailing Newline
    command[strcspn(command, "\n")] = '\0';

    // Prints the "<command>: command not found" message
    printf("%s: command not found\n", command);
  }

  return 0;
}
