#include <stdio.h>
#include <stdlib.h>

#include "readline.h"
#include "vendor/mpc/mpc.h"

#define LASSERT(args, cond, err) \
  if (!(cond)) { lval_del(args); return lval_err(err); }

const char* PROMPT = "lithp> ";
const char* VERSION = "0.0.0";

typedef enum {
  LVAL_SYM,
  LVAL_SEXPR,
  LVAL_QEXPR,
  LVAL_NUM,
  LVAL_ERR
} lval_type;

typedef struct lval {
  lval_type type;

  char* err;
  char* sym;
  long num;

  int count;
  struct lval** cell;
} lval;

lval* lval_pop(lval* val, int i);
lval* lval_eval(lval* val);
void lval_print(lval* val);

lval* lval_qexpr(void) {
  lval* val = malloc(sizeof(lval));
  val->type = LVAL_QEXPR;
  val->cell = NULL;
  val->count = 0;
  return val;
}

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

lval* lval_err(char* err) {
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
    case LVAL_QEXPR:
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
    lval_num(x) : lval_err("bad number");
}

lval* lval_add(lval* val, lval* child) {
  val->count++;
  val->cell = realloc(val->cell, sizeof(lval*) * val->count);
  val->cell[val->count - 1] = child;
  return val;
}

lval* lval_join(lval* holder, lval* drained) {
  while (drained->count) {
    holder = lval_add(holder, lval_pop(drained, 0));
  }

  lval_del(drained);

  return holder;
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
  if (strstr(t->tag, "qexpr"))
    val = lval_qexpr();

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

    case LVAL_QEXPR:
      lval_expr_print(val, '{', '}');
      break;

    case LVAL_NUM:
      printf("%li", val->num);
      break;

    case LVAL_ERR:
      printf("error: %s", val->err);
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

/**
 * extracts a single element from an S-Expression at the index i and shifts the
 * rest of the list backward so that it no longer contains that `lval*`. It
 * then returns the extracted value. Notice that it doesn't delete the input
 * list. It is like taking an element from a list and popping it out, leaving
 * what remains. This means that both the element popped and the old list need
 * to be deleted at some ppoint with `lval_del`.
 */
lval* lval_pop(lval* val, int i) {
  lval* child = val->cell[i];

  memmove(&val->cell[i], &val->cell[i + 1],
    sizeof(lval*) * (val->count - 1));

  val->count--;
  val->cell = realloc(val->cell, sizeof(lval*) * val->count);

  return child;
}

/**
 * similar to `lval_pop` but it deletes the list it has extracted the element
 * from. This is like taking an element from the list and deleting the rest. It
 * is a slight variation on `lval_pop` but it makes our code easier to read in
 * some places. Unlike `lval_pop`, only the expression you take from the list
 * needs to be deleted by `lval_del`.
 */
lval* lval_take(lval* val, int i) {
  lval* child = lval_pop(val, i);
  lval_del(val);
  return child;
}

lval* builtin_head(lval* args) {
  LASSERT(args, args->count == 1,
    "Function 'head' expects one argument.");
  LASSERT(args, args->cell[0]->type == LVAL_QEXPR,
    "Function 'head' expects a Q-Expression.");
  LASSERT(args, args->cell[0]->count != 0,
    "Function 'head' passed an empty Q-Expression.");

  // get the first argument then pop all elements from it until we have just
  // one and return that
  lval* arg = lval_take(args, 0);

  while (arg->count > 1) {
    lval_del(lval_pop(arg, 1));
  }

  return arg;
}

lval* builtin_tail(lval* args) {
  LASSERT(args, args->count == 1,
    "Function 'tail' expects one argument.");
  LASSERT(args, args->cell[0]->type == LVAL_QEXPR,
    "Function 'tail' expects a Q-Expression.");
  LASSERT(args, args->cell[0]->count != 0,
    "Function 'tail' passed an empty Q-Expression.");

  // take the first argument, delete the first child and return it
  lval* arg = lval_take(args, 0);
  lval_del(lval_pop(arg, 0));

  return arg;
}

lval* builtin_list(lval* args) {
  args->type = LVAL_QEXPR;
  return args;
}

lval* builtin_eval(lval* args) {
  LASSERT(args, args->count == 1,
    "Function 'eval' expects one argument.");
  LASSERT(args, args->cell[0]->type == LVAL_QEXPR,
    "Function 'eval' expects a Q-Expression.");

  lval* arg = lval_take(args, 0);
  arg->type = LVAL_SEXPR;

  return lval_eval(arg);
}

lval* builtin_join(lval* args) {
  for (int i = 0; i < args->count; i++) {
    LASSERT(args, args->cell[i]->type == LVAL_QEXPR,
      "Function 'join' expects only Q-Expressions.");
  }

  lval* joined = lval_pop(args, 0);

  while (args->count) {
    joined = lval_join(joined, lval_pop(args, 0));
  }

  lval_del(args);

  return joined;
}

lval* builtin_len(lval* args) {
  LASSERT(args, args->count == 1,
    "Function 'len' expects one argument.");
  LASSERT(args, args->cell[0]->type == LVAL_QEXPR,
    "Function 'len' expects a Q-Expression.");

  long len = args->cell[0]->count;
  lval_del(args);

  return lval_num(len);
}

lval* builtin_op(lval* val, char* op) {
  for (int i = 0; i < val->count; i++) {
    if (val->cell[i]->type != LVAL_NUM) {
      lval_del(val);
      return lval_err("cannot operate on a non-number");
    }
  }

  lval* head = lval_pop(val, 0);

  // negatives
  if ((strcmp(op, "-") == 0) && val->count == 0) {
    val->num = -val->num;
  }

  while (val->count > 0) {
    lval* next = lval_pop(val, 0);

    if (strcmp(op, "+") == 0) {
      head->num += next->num;
    } else if (strcmp(op, "-") == 0) {
      head->num -= next->num;
    } else if (strcmp(op, "*") == 0) {
      head->num *= next->num;
    } else if (strcmp(op, "/") == 0) {
      if (next->num == 0) {
        lval_del(head);
        lval_del(next);

        head = lval_err("cannot devide by zero");
        break;
      }

      head->num /= next->num;
    }

    lval_del(next);
  }

  lval_del(val);
  return head;
}

lval* builtin(lval* args, char* func) {
  if (strcmp("list", func) == 0) {
    return builtin_list(args);
  } else if (strcmp("head", func) == 0) {
    return builtin_head(args);
  } else if (strcmp("tail", func) == 0) {
    return builtin_tail(args);
  } else if (strcmp("join", func) == 0) {
    return builtin_join(args);
  } else if (strcmp("eval", func) == 0) {
    return builtin_eval(args);
  } else if (strcmp("len", func) == 0) {
    return builtin_len(args);
  } else if (strstr("+-/*", func)) {
    return builtin_op(args, func);
  }

  lval_del(args);

  return lval_err(strcat("Unknow function: ", func));
}

lval* lval_eval_sexpr(lval* val) {
  for (int i = 0; i < val->count; i++) {
    val->cell[i] = lval_eval(val->cell[i]);
  }

  for (int i = 0; i < val->count; i++) {
    if (val->cell[i]->type == LVAL_ERR) {
      return lval_take(val, i);
    }
  }

  if (val->count == 0) {
    return val;
  }

  if (val->count == 1) {
    return lval_take(val, 0);
  }

  lval* head = lval_pop(val, 0);

  if (head->type != LVAL_SYM) {
    lval_del(head);
    lval_del(val);
    return lval_err("s-expression does not start with symbol");
  }

  lval* result = builtin(val, head->sym);
  lval_del(head);

  return result;
}

lval* lval_eval(lval* val) {
  if (val->type == LVAL_SEXPR) {
    return lval_eval_sexpr(val);
  }

  return val;
}

int main() {
  char* grammar = read("grammar.txt");

  if (!grammar) {
    printf("failed to read grammar file");
    exit(EXIT_FAILURE);
  }

  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Symbol = mpc_new("symbol");
  mpc_parser_t* Sexpr = mpc_new("sexpr");
  mpc_parser_t* Qexpr = mpc_new("qexpr");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lithp = mpc_new("lithp");

  mpca_lang(MPCA_LANG_DEFAULT, grammar,
    Number, Symbol, Sexpr, Qexpr, Expr, Lithp);
  free(grammar);

  printf("Lithp Version %s\n", VERSION);
  printf("Press Ctrl+c to Exit\n\n");

  while (1) {
    mpc_result_t result;
    char* input = readline(PROMPT);

    if (mpc_parse("<stdin>", input, Lithp, &result)) {
      lval* val = lval_eval(lval_read(result.output));
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

  mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Lithp);

  return 0;
}
