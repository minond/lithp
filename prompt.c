#include <stdio.h>
#include <stdlib.h>

#include "readline.h"
#include "vendor/mpc/mpc.h"

#define UNUSED(x) (void)(x)
#define LASSERT(args, cond, fmt, ...) \
  if (!(cond)) { \
    lval* err = lval_err(fmt, ##__VA_ARGS__); \
    lval_del(args); \
    return err; \
  }

const char* PROMPT = "lithp> ";
const char* VERSION = "0.0.0";

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

typedef lval*(*lbuiltin)(lenv*, lval*);

typedef enum {
  LVAL_FUN,
  LVAL_SYM,
  LVAL_SEXPR,
  LVAL_QEXPR,
  LVAL_NUM,
  LVAL_ERR
} lval_type;

struct lval {
  lval_type type;

  long num;
  char* err;
  char* sym;
  lbuiltin fun;

  int count;
  struct lval** cell;
};

struct lenv {
  int count;
  char** syms;
  lval** vals;
};

lval* lval_pop(lval*, int);
lval* lval_eval(lenv*, lval*);
lval* lval_copy(lval*);
void lval_print(lval*);

lval* builtin_op(lenv*, lval*, char*);

lval* lval_qexpr(void) {
  lval* val = malloc(sizeof(lval));
  val->type = LVAL_QEXPR;
  val->cell = NULL;
  val->count = 0;
  return val;
}

lval* lval_fun(lbuiltin func) {
  lval* val = malloc(sizeof(lval));
  val->type = LVAL_FUN;
  val->fun = func;
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

lval* lval_err(char* fmt, ...) {
  lval* val = malloc(sizeof(lval));
  val->type = LVAL_ERR;

  va_list args;
  va_start(args, fmt);

  val->err = malloc(512);
  vsnprintf(val->err, 511, fmt, args);
  val->err = realloc(val->err, strlen(val->err) + 1);

  va_end(args);
  return val;
}

void lval_del(lval* val) {
  switch (val->type) {
    case LVAL_FUN: break;
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

lenv* lenv_new(void) {
  lenv* env = malloc(sizeof(lenv));
  env->count = 0;
  env->syms = NULL;
  env->vals = NULL;
  return env;
}

void lenv_del(lenv* env) {
  for (int i = 0; i < env->count; i++) {
    free(env->syms[i]);
    lval_del(env->vals[i]);
  }

  free(env->syms);
  free(env->vals);
  free(env);
}

/**
 * to get a value from the environment we loop over all the items in the
 * environment and check if the given symbol matches any of the stored strings.
 * if we find a match we can return a copy of the stored value. if no match is
 * found we should return an error.
 */
lval* lenv_get(lenv* env, lval* label) {
  for (int i = 0; i < env->count; i++) {
    if (strcmp(env->syms[i], label->sym) == 0) {
      return lval_copy(env->vals[i]);
    }
  }

  return lval_err("Unbound symbol '%s'!", label->sym);
}

/**
 * the function for putting new variables into the environment is a little bit
 * more complex. first we want to check if a variable with the same name
 * already exists. if this is the case we should replace its value with the new
 * one. to do this we loop over all the existing variables in the environment
 * and check their name. if a match is found we delete the value stored at that
 * location, and store there a copy of the input value. if no existing value is
 * found with that name, we need to allocate some more space to out it in. for
 * this we can use `realloc`, and store a copy of the `lval` and its name at
 * the newly allocated locations.
 */
void lenv_put(lenv* env, lval* label, lval* value) {
  for (int i = 0; i < env->count; i++) {
    if (strcmp(env->syms[i], label->sym) == 0) {
      lval_del(env->vals[i]);
      env->vals[i] = lval_copy(value);
      return;
    }
  }

  env->count++;

  env->vals = realloc(env->vals, sizeof(lval*) * env->count);
  env->syms = realloc(env->syms, sizeof(char*) * env->count);

  env->vals[env->count - 1] = lval_copy(value);
  env->syms[env->count - 1] = malloc(strlen(label->sym) + 1);
  strcpy(env->syms[env->count - 1], label->sym);
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

char* ltype_name(lval_type type) {
  switch (type) {
    case LVAL_FUN: return "Function";
    case LVAL_SYM: return "Symbol";
    case LVAL_SEXPR: return "S-Expression";
    case LVAL_QEXPR: return "Q-Expression";
    case LVAL_NUM: return "Number";
    case LVAL_ERR: return "Error";
    default: return "Unknown type";
  }
}

void lval_print(lval* val) {
  switch (val->type) {
    case LVAL_FUN:
      printf("<function>");
      break;

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
      printf("Error: %s", val->err);
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
 * useful when we put things into, and take things out of, the
 * environment. for number and function s we can just copy the relevant
 * fields directly. for strings we need to copy using `malloc` and
 * `strcpy`. to copy lists we need to allocate the correct amount of
 * space and then copy each element individually.
 */
lval* lval_copy(lval* source) {
  lval* target = malloc(sizeof(lval));
  target->type = source->type;

  switch (source->type) {
    case LVAL_FUN:
      target->fun = source->fun;
      break;

    case LVAL_NUM:
      target->num = source->num;
      break;

    case LVAL_ERR:
      target->err = malloc(strlen(source->err) + 1);
      strcpy(target->err, source->err);
      break;

    case LVAL_SYM:
      target->sym = malloc(strlen(source->sym) + 1);
      strcpy(target->sym, source->sym);
      break;

    case LVAL_SEXPR:
    case LVAL_QEXPR:
      target->count = source->count;
      target->cell = malloc(sizeof(lval*) * target->count);

      for (int i = 0; i < target->count; i++) {
        target->cell[i] = lval_copy(source->cell[i]);
      }

      break;
  }

  return target;
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

lval* builtin_head(lenv* env, lval* args) {
  UNUSED(env);

  LASSERT(args, args->count == 1,
    "Function 'head' expects one argument but got %i.", args->count);
  LASSERT(args, args->cell[0]->type == LVAL_QEXPR,
    "Function 'tail' expects a Q-Expression but got (a/an) %s instead.", ltype_name(args->cell[0]->type));
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

lval* builtin_tail(lenv* env, lval* args) {
  UNUSED(env);

  LASSERT(args, args->count == 1,
    "Function 'tail' expects one argument but got %i.", args->count);
  LASSERT(args, args->cell[0]->type == LVAL_QEXPR,
    "Function 'tail' expects a Q-Expression but got (a/an) %s instead.", ltype_name(args->cell[0]->type));
  LASSERT(args, args->cell[0]->count != 0,
    "Function 'tail' passed an empty Q-Expression.");

  // take the first argument, delete the first child and return it
  lval* arg = lval_take(args, 0);
  lval_del(lval_pop(arg, 0));

  return arg;
}

lval* builtin_list(lenv* env, lval* args) {
  UNUSED(env);
  args->type = LVAL_QEXPR;
  return args;
}

lval* builtin_eval(lenv* env, lval* args) {
  LASSERT(args, args->count == 1,
    "Function 'eval' expects one argument but got %i.", args->count);
  LASSERT(args, args->cell[0]->type == LVAL_QEXPR,
    "Function 'eval' expects a Q-Expression but got (a/an) %s instead.", ltype_name(args->cell[0]->type));

  lval* arg = lval_take(args, 0);
  arg->type = LVAL_SEXPR;

  return lval_eval(env, arg);
}

lval* builtin_join(lenv* env, lval* args) {
  UNUSED(env);

  for (int i = 0; i < args->count; i++) {
    LASSERT(args, args->cell[i]->type == LVAL_QEXPR,
      "Function 'join' expects only Q-Expressions but got (a/an) %s instead in index %i.",
        ltype_name(args->cell[1]->type), i);
  }

  lval* joined = lval_pop(args, 0);

  while (args->count) {
    joined = lval_join(joined, lval_pop(args, 0));
  }

  lval_del(args);

  return joined;
}

lval* builtin_cons(lenv* env, lval* args) {
  UNUSED(env);

  LASSERT(args, args->count == 2,
    "Function 'cons' expects two arguments but got %i.", args->count);
  LASSERT(args, args->cell[1]->type == LVAL_QEXPR,
    "Function 'cons' expects a value and a Q-Expression but got (a/an) %s instead.", ltype_name(args->cell[1]->type));

  lval* list = lval_sexpr();
  lval* head = lval_pop(args, 0);
  lval* body = lval_pop(args, 0);

  lval_add(list, head);
  lval_join(list, body);
  lval_del(args);

  return list;
}

lval* builtin_len(lenv* env, lval* args) {
  UNUSED(env);

  LASSERT(args, args->count == 1,
    "Function 'len' expects two arguments but got %i.", args->count);
  LASSERT(args, args->cell[0]->type == LVAL_QEXPR,
    "Function 'len' expects a Q-Expression but got (a/an) %s instead.", ltype_name(args->cell[0]->type));

  long len = args->cell[0]->count;
  lval_del(args);

  return lval_num(len);
}

lval* builtin_add(lenv* env, lval* val) {
  return builtin_op(env, val, "+");
}

lval* builtin_sub(lenv* env, lval* val) {
  return builtin_op(env, val, "-");
}

lval* builtin_mul(lenv* env, lval* val) {
  return builtin_op(env, val, "*");
}

lval* builtin_div(lenv* env, lval* val) {
  return builtin_op(env, val, "/");
}

lval* builtin_op(lenv* env, lval* val, char* op) {
  UNUSED(env);

  for (int i = 0; i < val->count; i++) {
    if (val->cell[i]->type != LVAL_NUM) {
      lval* err = lval_err("Function '%s' expects a %s but got (a/an) %s instead on index %i.",
        op, ltype_name(LVAL_NUM), ltype_name(val->cell[i]->type), i);

      lval_del(val);
      return err;
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

/**
 * this function should act like any other builtin. it first checks for error
 * conditions and then performs some command and returns a value. in this case
 * it first checks that the input arguments are the correct types. it then
 * iterates over each symbol and value and puts them into the environment. if
 * there is an error we can return it, but on success we will return the empty
 * expression `()`.
 */
lval* builtin_def(lenv* env, lval* args) {
  LASSERT(args, args->cell[0]->type == LVAL_QEXPR,
    "Function 'def' expects a Q-Expression.");

  lval* syms = args->cell[0];

  for (int i = 0; i < syms->count; i++) {
    LASSERT(args, syms->cell[i]->type == LVAL_SYM,
      "Function 'def' cannot define non-symbol");
  }

  LASSERT(args, syms->count == args->count - 1,
    "Function 'def' cannot define incorrect number of values to symbols");

  for (int i = 0; i < syms->count; i++) {
    lenv_put(env, syms->cell[i], args->cell[i + 1]);
  }

  lval_del(args);
  return lval_sexpr();
}

/**
 * the environment always takes or returns copies of a value, so we need to
 * remember to delete these two `lval` after registration as we won't need them
 * any more.
 */
void lenv_add_builtin(lenv* env, char* name, lbuiltin func) {
  lval* label = lval_sym(name);
  lval* value = lval_fun(func);

  lenv_put(env, label, value);
  lval_del(label);
  lval_del(value);
}

void lenv_add_builtins(lenv* env) {
  lenv_add_builtin(env, "def", builtin_def);

  lenv_add_builtin(env, "list", builtin_list);
  lenv_add_builtin(env, "head", builtin_head);
  lenv_add_builtin(env, "tail", builtin_tail);
  lenv_add_builtin(env, "eval", builtin_eval);
  lenv_add_builtin(env, "join", builtin_join);
  lenv_add_builtin(env, "cons", builtin_cons);
  lenv_add_builtin(env, "len",  builtin_len);

  lenv_add_builtin(env, "+", builtin_add);
  lenv_add_builtin(env, "-", builtin_sub);
  lenv_add_builtin(env, "*", builtin_mul);
  lenv_add_builtin(env, "/", builtin_div);
}

lval* lval_eval_sexpr(lenv* env, lval* val) {
  for (int i = 0; i < val->count; i++) {
    val->cell[i] = lval_eval(env, val->cell[i]);
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

  if (head->type != LVAL_FUN) {
    lval_del(head);
    lval_del(val);
    return lval_err("first element is not a function");
  }

  lval* result = head->fun(env, val);
  lval_del(head);

  return result;
}

lval* lval_eval(lenv* env, lval* val) {
  if (val->type == LVAL_SYM) {
    lval* ret = lenv_get(env, val);
    lval_del(val);
    return ret;
  }

  if (val->type == LVAL_SEXPR) {
    return lval_eval_sexpr(env, val);
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

  lenv* env = lenv_new();
  lenv_add_builtins(env);

  printf("Lithp Version %s\n", VERSION);
  printf("Press Ctrl+c to Exit\n\n");

  while (1) {
    mpc_result_t result;
    char* input = readline(PROMPT);

    if (mpc_parse("<stdin>", input, Lithp, &result)) {
      lval* val = lval_eval(env, lval_read(result.output));
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

  lenv_del(env);
  mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Lithp);

  return 0;
}
