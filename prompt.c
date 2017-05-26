#include <stdio.h>
#include <stdlib.h>

#include "readline.h"

const char * PROMPT = "lithp> ";
const char * VERSION = "0.0.0";

int main(int argc, char ** argv) {
  printf("Lispy Version %s\n", VERSION);
  printf("Press Ctrl+c to Exit\n\n");

  while (1) {
    char * input = readline(PROMPT);

    add_history(input);
    printf("got %s\n", input);
    free(input);
  }

  return 0;
}
