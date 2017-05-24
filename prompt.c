#include <stdio.h>
#include <stdlib.h>

const char * PROMPT = "lithp> ";
const char * VERSION = "0.0.0";

#ifdef _WIN32
#include <string.h>

const size_t INPUT_SIZE = 2048;
static char INPUT[INPUT_SIZE];

void add_history(char * ignore) {}

char * readline(char * prompt) {
  fputs(prompt, stdout);
  fgets(INPUT, INPUT_SIZE, stdin);

  char * tmp = malloc(strlen(INPUT) + 1);
  strcpy(tmp, INPUT);
  tmp[strlen(tmp) - 1] = '\0';

  return tmp;
}

#else
#include <editline/readline.h>
#endif

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
