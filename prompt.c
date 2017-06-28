#include <stdio.h>
#include <stdlib.h>

#include "readline.h"
#include "vendor/mpc/mpc.h"

const char* PROMPT = "lithp> ";
const char* VERSION = "0.0.0";

typedef enum {
  LVAL_SYM,
  LVAL_SEXPR,
  LVAL_NUM,
  LVAL_ERR
} lval_type;

typedef enum {
  LERR_DIV_ZERO,
  LERR_BAD_OP,
  LERR_BAD_NUM
} lerr;

typedef struct lval {
  lval_type type;

  lerr err;
  char* sym;
  long num;

  int count;
  struct lval** cell;
} lval;

void lval_print(lval* val);

lval* lval_sexpr(void) {
  lval* val = malloc(sizeof(lval));
  val->type = LVAL_SEXPR;
  val->cell = NULL;
  val->count = 0;
  return val;
}

lval* lval_sym(char* sym) {
  lval* val = malloc(sizeof(lval));
  val->type = LVAL_SYM;
  val->sym = malloc(strlen(sym) + 1);
  strcpy(val->sym, sym);
  return val;
}

lval* lval_num(long num) {
  lval* val = malloc(sizeof(lval));
  val->type = LVAL_NUM;
  val->num = num;
  return val;
}

lval* lval_err(lerr err) {
  lval* val = malloc(sizeof(lval));
  val->type = LVAL_ERR;
  val->err = err;
  return val;
}

void lval_del(lval* val) {
  switch (val->type) {
    case LVAL_NUM: break;
    case LVAL_ERR: break;
    case LVAL_SYM: free(val->sym); break;
    case LVAL_SEXPR:
      for (int i = 0; i < val->count; i++) {
        lval_del(val->cell[i]);
      }

      free(val->cell);
      break;
  }

  free(val);
}

lval* lval_read_num(mpc_ast_t* t) {
  errno = 0;
  long x = strtol(t->contents, NULL, 10);
  return errno != ERANGE ?
    lval_num(x) : lval_err(LERR_BAD_NUM);
}

lval* lval_add(lval* val, lval* child) {
  val->count++;
  val->cell = realloc(val->cell, sizeof(lval*) * val->count);
  val->cell[val->count - 1] = child;
  return val;
}

lval* lval_read(mpc_ast_t* t) {
  if (strstr(t->tag, "number"))
    return lval_read_num(t);
  if (strstr(t->tag, "symbol"))
    return lval_sym(t->contents);

  lval* val = NULL;

  if (strcmp(t->tag, ">") == 0)
    val = lval_sexpr();
  if (strstr(t->tag, "sexpr"))
    val = lval_sexpr();

  for (int i = 0; i < t->children_num; i++) {
    if (strcmp(t->children[i]->contents, "(") == 0)
      continue;
    if (strcmp(t->children[i]->contents, ")") == 0)
      continue;
    if (strcmp(t->children[i]->contents, "{") == 0)
      continue;
    if (strcmp(t->children[i]->contents, "}") == 0)
      continue;
    if (strcmp(t->children[i]->tag, "regex") == 0)
      continue;

    val = lval_add(val, lval_read(t->children[i]));
  }

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

void lval_expr_print(lval* val, char open, char close) {
  putchar(open);

  for (int i = 0; i < val->count; i++) {
    lval_print(val->cell[i]);

    if (i != (val->count - 1)) {
      putchar(' ');
    }
  }

  putchar(close);
}

void lval_print(lval* val) {
  switch (val->type) {
    case LVAL_SYM:
      printf("%s", val->sym);
      break;

    case LVAL_SEXPR:
      lval_expr_print(val, '(', ')');
      break;

    case LVAL_NUM:
      printf("%li", val->num);
      break;

    case LVAL_ERR:
      printf("error: %s", lval_err_string(val->err));
      break;
  }
}

void lval_println(lval* val) {
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

// lval eval_op(lval x, char* op, lval y) {
//   if (strcmp(op, "+") == 0)
//     return lval_num(x.num + y.num);
//   else if (strcmp(op, "-") == 0)
//     return lval_num(x.num - y.num);
//   else if (strcmp(op, "*") == 0)
//     return lval_num(x.num * y.num);
//   else if (strcmp(op, "/") == 0) {
//     if (y.num == 0)
//       return lval_err(LERR_DIV_ZERO);
//     else
//       return lval_num(x.num / y.num);
//   } else if (strcmp(op, "min") == 0)
//     return lval_num(x.num < y.num ? x.num : y.num);
//   else if (strcmp(op, "max") == 0)
//     return lval_num(x.num < y.num ? y.num : x.num);
//   else
//     return lval_err(LERR_BAD_OP);
// }

// lval eval(mpc_ast_t* node) {
//   if (strstr(node->tag, "number")) {
//     errno = 0;
//     long num = strtol(node->contents, NULL, 10);
//     return errno != ERANGE ? lval_num(num) : lval_err(LERR_BAD_NUM);
//   }
//
//   // children[0] = '('
//   // children[children_num] = ')'
//   int i = 1;
//   char* op = node->children[i++]->contents;
//   lval val = eval(node->children[i++]);
//
//   while (strstr(node->children[i]->tag, "expr"))
//     val = eval_op(val, op, eval(node->children[i++]));
//
//   return val;
// }

int main() {
  char* grammar = read("grammar.txt");

  if (!grammar) {
    printf("failed to read grammar file");
    exit(EXIT_FAILURE);
  }

  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Symbol = mpc_new("symbol");
  mpc_parser_t* Sexpr = mpc_new("sexpr");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lithp = mpc_new("lithp");

  mpca_lang(MPCA_LANG_DEFAULT, grammar,
    Number, Symbol, Sexpr, Expr, Lithp);
  free(grammar);

  printf("Lithp Version %s\n", VERSION);
  printf("Press Ctrl+c to Exit\n\n");

  while (1) {
    mpc_result_t result;
    char* input = readline(PROMPT);

    if (mpc_parse("<stdin>", input, Lithp, &result)) {
      // lval_println(eval(result.output));
      lval* val = lval_read(result.output);
      lval_println(val);
      lval_del(val);
      mpc_ast_delete(result.output);
    } else {
      mpc_err_print(result.error);
      mpc_err_delete(result.error);
    }

    add_history(input);
    free(input);
  }

  mpc_cleanup(5, Number, Symbol, Sexpr, Expr, Lithp);

  return 0;
}
