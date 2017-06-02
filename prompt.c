#include <stdio.h>
#include <stdlib.h>

#include "readline.h"
#include "vendor/mpc/mpc.h"

const char* PROMPT = "lithp> ";
const char* VERSION = "0.0.0";

typedef enum {
  LVAL_NUM,
  LVAL_ERR
} lval_type;

typedef enum {
  LERR_DIV_ZERO,
  LERR_BAD_OP,
  LERR_BAD_NUM
} lerr;

typedef struct {
  lval_type type;
  lerr err;
  long num;
} lval;

lval lval_num(long num) {
  lval val;

  val.type = LVAL_NUM;
  val.num = num;

  return val;
}

lval lval_err(lerr err) {
  lval val;

  val.type = LVAL_ERR;
  val.err = err;

  return val;
}

char* lval_err_string(lerr err) {
  switch (err) {
  case LERR_BAD_NUM: return "bad number";
  case LERR_BAD_OP: return "bad operator";
  case LERR_DIV_ZERO: return "cannot devide by zero";
  default: return "unknown error";
  }
}

void lval_print(lval val) {
  switch (val.type) {
  case LVAL_NUM:
    printf("%li", val.num);
    break;

  case LVAL_ERR:
    printf("error: %s", lval_err_string(val.err));
    break;
  }
}

void lval_println(lval val) {
  lval_print(val);
  putchar('\n');
}

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

lval eval_op(lval x, char* op, lval y) {
  if (strcmp(op, "+") == 0)
    return lval_num(x.num + y.num);
  else if (strcmp(op, "-") == 0)
    return lval_num(x.num - y.num);
  else if (strcmp(op, "*") == 0)
    return lval_num(x.num * y.num);
  else if (strcmp(op, "/") == 0) {
    if (y.num == 0)
      return lval_err(LERR_DIV_ZERO);
    else
      return lval_num(x.num / y.num);
  } else if (strcmp(op, "min") == 0)
    return lval_num(x.num < y.num ? x.num : y.num);
  else if (strcmp(op, "max") == 0)
    return lval_num(x.num < y.num ? y.num : x.num);
  else
    return lval_err(LERR_BAD_OP);
}

lval eval(mpc_ast_t* node) {
  if (strstr(node->tag, "number")) {
    errno = 0;
    long num = strtol(node->contents, NULL, 10);
    return errno != ERANGE ? lval_num(num) : lval_err(LERR_BAD_NUM);
  }

  // children[0] = '('
  // children[children_num] = ')'
  int i = 1;
  char* op = node->children[i++]->contents;
  lval val = eval(node->children[i++]);

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
      lval_println(eval(result.output));
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
