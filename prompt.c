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

#define LASSERT_ARG_COUNT(args, func, expected) \
  LASSERT(args, expected == args->count, \
    "Function '%s' expects %i argument but got %i.", \
      func, expected, args->count);

#define LASSERT_ARG_TYPE_AT(args, func, expected, index) \
  LASSERT(args, args->cell[index]->type == expected, \
    "Function '%s' expects a %s but got (a/an) %s at index %i instead.", \
      func, ltype_name(expected), ltype_name(args->cell[index]->type), index);

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

  // basic
  long num;
  char* err;
  char* sym;

  // function
  lbuiltin builtin;
  lenv* env;
  lval* formals;
  lval* body;

  // expression
  int count;
  struct lval** cell;
};

struct lenv {
  int count;
  lenv* par;
  char** syms;
  lval** vals;
};

lval* lval_call(lenv*, lval*, lval*);
lval* lval_pop(lval*, int);
lval* lval_eval(lenv*, lval*);
lval* lval_copy(lval*);
lenv* lenv_new();
void lenv_del(lenv* env);
void lval_print(lval*);

lval* builtin_op(lenv*, lval*, char*);

lval* lval_qexpr(void) {
  lval* val = malloc(sizeof(lval));
  val->type = LVAL_QEXPR;
  val->cell = NULL;
  val->count = 0;
  return val;
}

lval* lval_builtin(lbuiltin func) {
  lval* val = malloc(sizeof(lval));
  val->type = LVAL_FUN;
  val->builtin = func;
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

lval* lval_lambda(lval* formals, lval* body) {
  lval* val = malloc(sizeof(lval));

  val->type = LVAL_FUN;
  val->builtin = NULL;
  val->env = lenv_new();
  val->formals = formals;
  val->body = body;

  return val;
}

void lval_del(lval* val) {
  switch (val->type) {
    case LVAL_NUM: break;

    case LVAL_ERR:
      free(val->err);
      break;

    case LVAL_SYM:
      free(val->sym);
      break;

    case LVAL_FUN:
      if (!val->builtin) {
        lenv_del(val->env);
        lval_del(val->formals);
        lval_del(val->body);
      }
      break;

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

lenv* lenv_new() {
  lenv* env = malloc(sizeof(lenv));
  env->count = 0;
  env->par = NULL;
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

  if (env->par) {
    return lenv_get(env->par, label);
  } else {
    return lval_err("Unbound symbol '%s'!", label->sym);
  }
}

/**
 * because we have a new `lval` type that has it own environment we need a
 * function for copying environments, to use for when we copy `lval` structs.
 */
lenv* lenv_copy(lenv* env) {
  int count = env->count;
  lenv* copy = malloc(sizeof(lenv));

  copy->count = count;
  copy->par = env->par;
  copy->syms = malloc(sizeof(char*) * count);
  copy->vals = malloc(sizeof(lval*) * count);

  for (int i = 0; i < count; i++) {
    copy->vals[i] = lval_copy(env->vals[i]);
    copy->syms[i] = malloc(strlen(env->syms[i]) + 1);
    strcpy(copy->syms[i], env->syms[i]);
  }

  return copy;
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
      env->syms[i] = realloc(env->syms[i], strlen(label->sym) + 1);
      strcpy(env->syms[i], label->sym);
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

/**
 * having parent environments also changes our concept of defining a variable.
 * there are two ways we could define a variable now. either we could define it
 * in local, innermost environment., or we could define it in the global,
 * outermost environment. we will add functions to do both. we'll leave the
 * `lenv_put` method the same. it can be used for definition in the local
 * environment. but we'll add a new function `lenv_def` for definition in the
 * global environment. this works by simply following the parent chain up
 * before using lval_put to define locally.
 */
void lenv_def(lenv* env, lval* label, lval* value) {
  while (env->par) env = env->par;
  lenv_put(env, label, value);
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
      if (val->builtin) {
        printf("<builtin>");
      } else {
        printf("(\\ ");
        lval_print(val->formals);
        putchar(' ');
        lval_print(val->body);
        putchar(')');
      }
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
      if (source->builtin) {
        target->builtin = source->builtin;
      } else {
        target->builtin = NULL;
        target->env = lenv_copy(source->env);
        target->formals = lval_copy(source->formals);
        target->body = lval_copy(source->body);
      }
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

  LASSERT_ARG_COUNT(args, "head", 1);
  LASSERT_ARG_TYPE_AT(args, "head", LVAL_QEXPR, 0);
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

  LASSERT_ARG_COUNT(args, "tail", 1);
  LASSERT_ARG_TYPE_AT(args, "tail", LVAL_QEXPR, 0);
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
  LASSERT_ARG_COUNT(args, "eval", 1);
  LASSERT_ARG_TYPE_AT(args, "eval", LVAL_QEXPR, 0);

  lval* arg = lval_take(args, 0);
  arg->type = LVAL_SEXPR;

  return lval_eval(env, arg);
}

lval* builtin_join(lenv* env, lval* args) {
  UNUSED(env);

  for (int i = 0; i < args->count; i++) {
    LASSERT_ARG_TYPE_AT(args, "join", LVAL_QEXPR, i);
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

  LASSERT_ARG_COUNT(args, "cons", 2);
  LASSERT_ARG_TYPE_AT(args, "cons", LVAL_QEXPR, 1);

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

  LASSERT_ARG_COUNT(args, "len", 1);
  LASSERT_ARG_TYPE_AT(args, "len", LVAL_QEXPR, 0);

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
lval* builtin_var(lenv* env, lval* args, char* func) {
  LASSERT_ARG_TYPE_AT(args, func, LVAL_QEXPR, 0);

  lval* syms = args->cell[0];

  for (int i = 0; i < syms->count; i++) {
    LASSERT(args, syms->cell[i]->type == LVAL_SYM,
      "Function '%s' cannot define non-symbol. Got %s but expected %s.",
        func, ltype_name(syms->cell[i]->type), ltype_name(LVAL_SYM));
  }

  LASSERT(args, syms->count == args->count - 1,
    "Function 'def' cannot define incorrect number of values to symbols");

  for (int i = 0; i < syms->count; i++) {
    if (strcmp(func, "def") == 0) {
      lenv_def(env, syms->cell[i], args->cell[i + 1]);
    }

    if (strcmp(func, "=") == 0) {
      lenv_put(env, syms->cell[i], args->cell[i + 1]);
    }
  }

  lval_del(args);
  return lval_sexpr();
}

lval* builtin_def(lenv* env, lval* args) {
  return builtin_var(env, args, "def");
}

lval* builtin_put(lenv* env, lval* args) {
  return builtin_var(env, args, "=");
}

/**
 * we can now add a builtin for our lambda function. we want it to take as
 * input some list of symbols, and a list that represents the code. after that
 * it should return a function lval. we've defined a few of builtins now, and
 * this one will follow the same format. like in `def` we do some error
 * checking to ensure that argument types and count are correct. then we just
 * pop the first two arguments from the list and pass them to our previously
 * defined function `lval_lambda`.
 */
lval* builtin_lambda(lenv* env, lval* args) {
  UNUSED(env);

  LASSERT_ARG_COUNT(args, "\\", 2);
  LASSERT_ARG_TYPE_AT(args, "\\", LVAL_QEXPR, 0);
  LASSERT_ARG_TYPE_AT(args, "\\", LVAL_QEXPR, 1);

  for (int i = 0; i < args->cell[0]->count; i++) {
    LASSERT(args, args->cell[0]->cell[i]->type == LVAL_SYM,
      "Cannot define non-symbol. Got %s but expected %s",
        ltype_name(args->cell[0]->cell[i]->type), ltype_name(LVAL_SYM));
  }

  lval* formals = lval_pop(args, 0);
  lval* body = lval_pop(args, 0);

  lval_del(args);
  return lval_lambda(formals, body);
}

/**
 * the environment always takes or returns copies of a value, so we need to
 * remember to delete these two `lval` after registration as we won't need them
 * any more.
 */
void lenv_add_builtin(lenv* env, char* name, lbuiltin func) {
  lval* label = lval_sym(name);
  lval* value = lval_builtin(func);

  lenv_put(env, label, value);
  lval_del(label);
  lval_del(value);
}

void lenv_add_builtins(lenv* env) {
  lenv_add_builtin(env, "\\",  builtin_lambda);
  lenv_add_builtin(env, "def", builtin_def);
  lenv_add_builtin(env, "=", builtin_put);

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

  // LVAL_FUN to handle function calls with no arguments
  // if (val->count == 1 && val->cell[0]->type != LVAL_FUN) {
  // seg fault on `+` for some reason!
  if (val->count == 1) {
    return lval_take(val, 0);
  }

  lval* head = lval_pop(val, 0);

  if (head->type != LVAL_FUN) {
    lval_del(head);
    lval_del(val);
    return lval_err("first element is not a function");
  }

  lval* result = lval_call(env, head, val);
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

/**
 * we need to write the code that runs when an expression gets evaluated and a
 * function `lval` is called. when this function type is a builtin we can call
 * it as before, using the function pointer, but we need to do something
 * separate for our user defined functions. we need to bind each of the
 * arguments passed in, to each of the symbols in the `formals` field. once
 * this is done we need to evaluate the `body` field. using the `env` field as
 * an evironemnt, and the calling environment as a parent.
 */
lval* lval_call(lenv* env, lval* formals, lval* args) {
  if (formals->builtin) {
    return formals->builtin(env, args);
  }

  int given = args->count;
  int total = formals->formals->count;

  while (args->count) {
    // if we've ran out of formal arguments to bind
    if (formals->formals->count == 0) {
      lval_del(args);

      return lval_err("Function passed too many arguments. Got %i but expected %i",
        given, total);
    }

    // pop the first symbol from the formals and pop the next arugment from the
    // arguments list
    lval* sym = lval_pop(formals->formals, 0);
    lval* val = lval_pop(args, 0);

    // bind a copy into the function's environment and then free them
    lenv_put(formals->env, sym, val);
    lval_del(sym);
    lval_del(val);
  }

  // arguments list is now bound so we can clean up
  lval_del(args);

  if (formals->formals->count == 0) {
    formals->env->par = env;

    // if all formals have been bond, evaluate and return
    return builtin_eval(formals->env,
      lval_add(lval_sexpr(), lval_copy(formals->body)));
  } else {
    // otherwise return partially evaluated function
    return lval_copy(formals);
  }
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
