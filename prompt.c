#include <stdio.h>
#include <stdlib.h>

#include "readline.h"
#include "vendor/mpc/mpc.h"

const char* PROMPT = "lithp> ";
const char* VERSION = "0.0.0";

// #include <limits.h>
//
// int int_reduce(int (*func)(int, int), int* ints, int id) {
//   int store = id;
//
//   int i = 0;
//   int len = sizeof(ints) / sizeof(int);
//
//   for (; i < len; i++)
//     store = (*func)(store, ints[i]);
//
//   return store;
// }
//
// int int_min_binary(int x, int y) {
//   return x < y ? x : y;
// }
//
// int int_min() {
//   int nums[3] = {2, 1, 3};
//   return int_reduce(int_min_binary, nums, INT_MAX);
// }

char* read(char* filename) {
  char* buffer = 0;
  long length;

  FILE* file = fopen(filename, "r");

  if (file) {
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);
    buffer = malloc(length);

    if (buffer) {
      fread(buffer, 1, length, file);
    }

    fclose(file);
  }

  return buffer;
}

long eval_op(long x, char* op, long y) {
  if (strcmp(op, "+") == 0)
    return x + y;
  if (strcmp(op, "-") == 0)
    return x - y;
  if (strcmp(op, "*") == 0)
    return x * y;
  if (strcmp(op, "/") == 0)
    return x / y;
  if (strcmp(op, "min") == 0)
    return x < y ? x : y;
  if (strcmp(op, "max") == 0)
    return x < y ? y : x;

  return 0l;
}

long eval(mpc_ast_t* node) {
  if (strstr(node->tag, "number")) {
    return atoi(node->contents);
  }

  // children[0] = '('
  // children[children_num] = ')'
  int i = 1;
  char* op = node->children[i++]->contents;
  long val = eval(node->children[i++]);

  while (strstr(node->children[i]->tag, "expr"))
    val = eval_op(val, op, eval(node->children[i++]));

  return val;
}

int main(int argc, char** argv) {
  char* grammar = read("grammar.txt");

  if (!grammar) {
    printf("failed to read grammar file");
    exit(EXIT_FAILURE);
  }

  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Operator = mpc_new("operator");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lithp = mpc_new("lithp");

  mpca_lang(MPCA_LANG_DEFAULT, grammar,
    Number, Operator, Expr, Lithp);
  free(grammar);

  printf("Lithp Version %s\n", VERSION);
  printf("Press Ctrl+c to Exit\n\n");

  while (1) {
    mpc_result_t result;
    char* input = readline(PROMPT);

    if (mpc_parse("<stdin>", input, Lithp, &result)) {
      printf("%s%li\n", PROMPT, eval(result.output));
      mpc_ast_delete(result.output);
    } else {
      mpc_err_print(result.error);
      mpc_err_delete(result.error);
    }

    add_history(input);
    free(input);
  }

  mpc_cleanup(4, Number, Operator, Expr, Lithp);

  return 0;
}
