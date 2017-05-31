#include <stdio.h>
#include <stdlib.h>

#include "readline.h"
#include "vendor/mpc/mpc.h"

const char * PROMPT = "lithp> ";
const char * VERSION = "0.0.0";

int main(int argc, char ** argv) {
  mpc_parser_t * Number = mpc_new("number");
  mpc_parser_t * Operator = mpc_new("operator");
  mpc_parser_t * Expr = mpc_new("expr");
  mpc_parser_t * Lithp = mpc_new("lithp");

  mpca_lang(MPCA_LANG_DEFAULT,
    "                                                         \
    number    : /-?[0-9]+/;                                   \
    operator  : \"add\" | '+' | '-' | '*' | '/' ;             \
    expr      : <number > | '(' <operator> <expr>+ ')' ;      \
    lithp     : /^/ <operator> <expr>+ /$/;                   \
    ",
    Number, Operator, Expr, Lithp);

  printf("Lithp Version %s\n", VERSION);
  printf("Press Ctrl+c to Exit\n\n");

  while (1) {
    mpc_result_t result;
    char * input = readline(PROMPT);

    if (mpc_parse("<stdin>", input, Lithp, &result)) {
      mpc_ast_print(result.output);
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
