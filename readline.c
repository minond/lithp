#include <stdio.h>
#include <stdlib.h>

#include "readline.h"

#if defined(_WIN32) || defined(__linux__)
#include <string.h>

#define INPUT_SIZE 2048
static char INPUT[INPUT_SIZE];

void add_history(char * ignore) {
  (void) ignore;
}

char * readline(const char * prompt) {
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
