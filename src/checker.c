#include "velyr.h"

typedef struct Symbol {
  char * name;
  Type type;
  bool mut;
  int depth;
  struct Symbol * next;
}
Symbol;

typedef struct Scope {
  Symbol * symbols;
  struct Scope * parent;
  int depth;
}
Scope;

typedef struct {
  Program * program;
  Diagnostics * diag;
  Arena * arena;
  Scope * scope;
  Task * task;
  int loop_depth, parallel_depth, parallel_body_depth;
  Symbol * reduction_symbol;
}
Checker;

static
const char * type_name(Type t) {
  switch (t) {
  case TY_VOID:
    return "void";
  case TY_I64:
    return "i64";
  case TY_F64:
    return "f64";
  case TY_BOOL:
    return "bool";
  case TY_STR:
    return "str";
  default:
    return "unknown";
  }
}
static bool numeric(Type t) {
  return t == TY_I64 || t == TY_F64;
}
static bool assignable(Type dst, Type src) {
  return dst == src || (dst == TY_F64 && src == TY_I64);
}

static Task * find_task(Program * p,
  const char * name) {
  for (Task * t = p -> tasks; t; t = t -> next)
    if (!strcmp(t -> name, name)) return t;
  return NULL;
}
static Symbol * find_symbol(Checker * c,
  const char * name) {
  for (Scope * s = c -> scope; s; s = s -> parent)
    for (Symbol * v = s -> symbols; v; v = v -> next)
      if (!strcmp(v -> name, name)) return v;
  return NULL;
}

static Symbol * add_symbol(Checker * c, char * name, Type type, bool mut, Token at) {
  for (Symbol * v = c -> scope -> symbols; v; v = v -> next)
    if (!strcmp(v -> name, name)) {
      diagnostic(c -> diag, at.line, at.col, "name error", "name is already declared in this scope");
      return v;
    }
  Symbol * v = arena_alloc(c -> arena, sizeof( * v));
  v -> name = name;
  v -> type = type;
  v -> mut = mut;
  v -> depth = c -> scope -> depth;
  v -> next = c -> scope -> symbols;
  c -> scope -> symbols = v;
  return v;
}

static void push_scope(Checker * c, Scope * s) {
  s -> symbols = NULL;
  s -> parent = c -> scope;
  s -> depth = c -> scope ? c -> scope -> depth + 1 : 0;
  c -> scope = s;
}
static void pop_scope(Checker * c) {
  c -> scope = c -> scope -> parent;
}
static void type_error(Checker * c, Token at,
  const char * msg) {
  diagnostic(c -> diag, at.line, at.col, "type error", msg);
}

static Type check_expr(Checker * c, Expr * e) {
  if (!e) return TY_VOID;
  switch (e -> kind) {
  case EX_INT:
    return e -> type = TY_I64;
  case EX_FLOAT:
    return e -> type = TY_F64;
  case EX_STRING:
    return e -> type = TY_STR;
  case EX_BOOL:
    return e -> type = TY_BOOL;
  case EX_VAR: {
    Symbol * v = find_symbol(c, e -> as.text);
    if (!v) {
      char m[160];
      snprintf(m, sizeof(m), "unknown variable '%s'", e -> as.text);
      type_error(c, e -> at, m);
      return e -> type = TY_UNKNOWN;
    }
    return e -> type = v -> type;
  }
  case EX_UNARY: {
    Type r = check_expr(c, e -> as.unary.right);
    if (e -> as.unary.op == TK_BANG || e -> as.unary.op == TK_NOT) {
      if (r != TY_BOOL) type_error(c, e -> at, "'not' requires bool");
      return e -> type = TY_BOOL;
    }
    if (!numeric(r)) type_error(c, e -> at, "unary sign requires a number");
    return e -> type = r;
  }
  case EX_BINARY: {
    Type l = check_expr(c, e -> as.binary.left), r = check_expr(c, e -> as.binary.right);
    TokenKind op = e -> as.binary.op;
    if (op == TK_AND || op == TK_AND_AND || op == TK_OR || op == TK_OR_OR) {
      if (l != TY_BOOL || r != TY_BOOL) type_error(c, e -> at, "logical operators require bool values");
      return e -> type = TY_BOOL;
    }
    if (op == TK_EQ_EQ || op == TK_BANG_EQ) {
      if (!(l == r || (numeric(l) && numeric(r)))) type_error(c, e -> at, "equality operands have incompatible types");
      return e -> type = TY_BOOL;
    }
    if (op == TK_LT || op == TK_LT_EQ || op == TK_GT || op == TK_GT_EQ) {
      if (!numeric(l) || !numeric(r)) type_error(c, e -> at, "ordering requires numbers");
      return e -> type = TY_BOOL;
    }
    if (!numeric(l) || !numeric(r)) {
      type_error(c, e -> at, "arithmetic requires numbers");
      return e -> type = TY_UNKNOWN;
    }
    if (op == TK_PERCENT && (l != TY_I64 || r != TY_I64)) type_error(c, e -> at, "'%' requires i64 operands");
    return e -> type = (l == TY_F64 || r == TY_F64) ? TY_F64 : TY_I64;
  }
  case EX_CALL: {
    const char * n = e -> as.call.name;
    ExprList * a = e -> as.call.args;
    if (!strcmp(n, "clock_ns") || !strcmp(n, "clock_resolution_ns") || !strcmp(n, "cycles") || !strcmp(n, "cpu_count")) {
      if (e -> as.call.argc) type_error(c, e -> at, "this builtin takes no arguments");
      for (; a; a = a -> next) check_expr(c, a -> expr);
      return e -> type = TY_I64;
    }
    if (!strcmp(n, "relax")) {
      if (e -> as.call.argc) type_error(c, e -> at, "relax() takes no arguments");
      for (; a; a = a -> next) check_expr(c, a -> expr);
      return e -> type = TY_VOID;
    }
    if (!strcmp(n, "sqrt")) {
      if (e -> as.call.argc != 1) type_error(c, e -> at, "sqrt() takes one number");
      Type x = a ? check_expr(c, a -> expr) : TY_UNKNOWN;
      if (!numeric(x)) type_error(c, e -> at, "sqrt() requires a number");
      if (a)
        for (a = a -> next; a; a = a -> next) check_expr(c, a -> expr);
      return e -> type = TY_F64;
    }
    if (!strcmp(n, "bench_report")) {
      static
      const Type expected[4] = {
        TY_STR,
        TY_I64,
        TY_I64,
        TY_I64
      };
      if (e -> as.call.argc != 4) type_error(c, e -> at, "bench_report() expects: label, iterations, elapsed_ns, elapsed_ticks");
      int index = 0;
      for (; a; a = a -> next, index++) {
        Type got = check_expr(c, a -> expr);
        if (index < 4 && !assignable(expected[index], got)) {
          char m[160];
          snprintf(m, sizeof(m), "bench_report argument %d needs %s, got %s", index + 1, type_name(expected[index]), type_name(got));
          type_error(c, a -> expr -> at, m);
        }
      }
      return e -> type = TY_VOID;
    }
    if (!strcmp(n, "read_line")) {
      if (e -> as.call.argc) type_error(c, e -> at, "read_line() takes no arguments");
      for (; a; a = a -> next) check_expr(c, a -> expr);
      return e -> type = TY_STR;
    }
    if (!strcmp(n, "eof")) {
      if (e -> as.call.argc) type_error(c, e -> at, "eof() takes no arguments");
      for (; a; a = a -> next) check_expr(c, a -> expr);
      return e -> type = TY_BOOL;
    }
    if (!strcmp(n, "is_number")) {
      if (e -> as.call.argc != 1) type_error(c, e -> at, "is_number() takes one str argument");
      if (a && check_expr(c, a -> expr) != TY_STR) type_error(c, e -> at, "is_number() requires a str");
      if (a)
        for (a = a -> next; a; a = a -> next) check_expr(c, a -> expr);
      return e -> type = TY_BOOL;
    }
    if (!strcmp(n, "parse_number")) {
      if (e -> as.call.argc != 1) type_error(c, e -> at, "parse_number() takes one str argument");
      if (a && check_expr(c, a -> expr) != TY_STR) type_error(c, e -> at, "parse_number() requires a str");
      if (a)
        for (a = a -> next; a; a = a -> next) check_expr(c, a -> expr);
      return e -> type = TY_F64;
    }
    Task * t = find_task(c -> program, n);
    if (!t) {
      char m[160];
      snprintf(m, sizeof(m), "unknown task '%s'", n);
      type_error(c, e -> at, m);
      for (; a; a = a -> next) check_expr(c, a -> expr);
      return e -> type = TY_UNKNOWN;
    }
    if (e -> as.call.argc != t -> param_count) {
      char m[160];
      snprintf(m, sizeof(m), "task '%s' expects %d arguments, got %d", n, t -> param_count, e -> as.call.argc);
      type_error(c, e -> at, m);
    }
    Param * q = t -> params;
    for (; a; a = a -> next) {
      Type got = check_expr(c, a -> expr);
      if (q) {
        if (!assignable(q -> type, got)) {
          char m[160];
          snprintf(m, sizeof(m), "argument needs %s, got %s", type_name(q -> type), type_name(got));
          type_error(c, a -> expr -> at, m);
        }
        q = q -> next;
      }
    }
    return e -> type = t -> return_type;
  }
  }
  return TY_UNKNOWN;
}

static void check_stmt(Checker * c, Stmt * s);
static void check_block(Checker * c, Stmt * b, bool new_scope) {
  Scope scope;
  if (new_scope) push_scope(c, & scope);
  for (Stmt * s = b -> as.block.first; s; s = s -> next) check_stmt(c, s);
  if (new_scope) pop_scope(c);
}

static bool guarantees_return(Stmt * s) {
  if (!s) return false;
  if (s -> kind == ST_RETURN) return true;
  if (s -> kind == ST_IF && s -> as.ifs.else_branch && guarantees_return(s -> as.ifs.then_branch) && guarantees_return(s -> as.ifs.else_branch)) return true;
  if (s -> kind == ST_BLOCK) {
    for (Stmt * x = s -> as.block.first; x; x = x -> next)
      if (guarantees_return(x)) return true;
  }
  return false;
}

static void check_stmt(Checker * c, Stmt * s) {
  switch (s -> kind) {
  case ST_BLOCK:
    check_block(c, s, true);
    break;
  case ST_DECL: {
    Type got = check_expr(c, s -> as.decl.init), type = s -> as.decl.declared == TY_UNKNOWN ? got : s -> as.decl.declared;
    if (type == TY_VOID || got == TY_VOID) type_error(c, s -> at, "variables cannot have type void");
    if (!assignable(type, got)) {
      char m[160];
      snprintf(m, sizeof(m), "cannot initialize %s with %s", type_name(type), type_name(got));
      type_error(c, s -> at, m);
    }
    add_symbol(c, s -> as.decl.name, type, s -> as.decl.mut, s -> at);
    s -> as.decl.declared = type;
    break;
  }
  case ST_ASSIGN: {
    Symbol * v = find_symbol(c, s -> as.assign.name);
    Type got = check_expr(c, s -> as.assign.value);
    if (!v) {
      char m[160];
      snprintf(m, sizeof(m), "unknown variable '%s'", s -> as.assign.name);
      type_error(c, s -> at, m);
      break;
    }
    if (!v -> mut) type_error(c, s -> at, "cannot assign to an immutable 'let' value");
    if (c -> parallel_depth && v -> depth < c -> parallel_body_depth && v != c -> reduction_symbol) type_error(c, s -> at, "parallel loop writes an outer variable without reduce(...)");
    if (s -> as.assign.op != TK_EQ && (!numeric(v -> type) || !numeric(got))) type_error(c, s -> at, "compound assignment requires numbers");
    else if (!assignable(v -> type, got)) type_error(c, s -> at, "assigned value has an incompatible type");
    break;
  }
  case ST_EXPR:
    check_expr(c, s -> as.expr.expr);
    break;
  case ST_IF:
    if (check_expr(c, s -> as.ifs.cond) != TY_BOOL) type_error(c, s -> at, "when condition must be bool");
    check_stmt(c, s -> as.ifs.then_branch);
    if (s -> as.ifs.else_branch) check_stmt(c, s -> as.ifs.else_branch);
    break;
  case ST_WHILE:
    if (check_expr(c, s -> as.whiles.cond) != TY_BOOL) type_error(c, s -> at, "while condition must be bool");
    c -> loop_depth++;
    check_stmt(c, s -> as.whiles.body);
    c -> loop_depth--;
    break;
  case ST_FOR: {
    if (check_expr(c, s -> as.fors.from) != TY_I64 || check_expr(c, s -> as.fors.to) != TY_I64) type_error(c, s -> at, "loop bounds must be i64");
    Symbol * red = NULL;
    if (s -> as.fors.reduction) {
      red = find_symbol(c, s -> as.fors.reduction);
      if (!red) type_error(c, s -> at, "reduction variable is not declared");
      else if (!red -> mut || !numeric(red -> type)) type_error(c, s -> at, "reduction variable must be a mutable number");
    }
    if (!s -> as.fors.parallel && s -> as.fors.reduction) type_error(c, s -> at, "reduce(...) is only valid on a parallel loop");
    Scope scope;
    push_scope(c, & scope);
    add_symbol(c, s -> as.fors.name, TY_I64, false, s -> at);
    int oldpd = c -> parallel_depth, oldbd = c -> parallel_body_depth;
    Symbol * oldred = c -> reduction_symbol;
    if (s -> as.fors.parallel) {
      c -> parallel_depth++;
      c -> parallel_body_depth = c -> scope -> depth;
      c -> reduction_symbol = red;
    }
    c -> loop_depth++;
    check_stmt(c, s -> as.fors.body);
    c -> loop_depth--;
    c -> parallel_depth = oldpd;
    c -> parallel_body_depth = oldbd;
    c -> reduction_symbol = oldred;
    pop_scope(c);
    break;
  }
  case ST_RETURN: {
    Type got = check_expr(c, s -> as.returns.value), want = c -> task ? c -> task -> return_type : TY_VOID;
    if (!c -> task) {
      type_error(c, s -> at, "'give' is only valid inside a task");
      break;
    }
    if (want == TY_VOID && s -> as.returns.value) type_error(c, s -> at, "void task cannot return a value");
    if (want != TY_VOID && !s -> as.returns.value) type_error(c, s -> at, "this task must return a value");
    if (want != TY_VOID && !assignable(want, got)) type_error(c, s -> at, "returned value has the wrong type");
    break;
  }
  case ST_SAY:
    for (ExprList * x = s -> as.says.values; x; x = x -> next) {
      Type t = check_expr(c, x -> expr);
      if (t == TY_VOID) type_error(c, x -> expr -> at, "cannot print void");
    }
    break;
  case ST_CHECK:
    if (check_expr(c, s -> as.checks.cond) != TY_BOOL) type_error(c, s -> at, "check condition must be bool");
    if (s -> as.checks.message && check_expr(c, s -> as.checks.message) != TY_STR) type_error(c, s -> at, "check message must be str");
    break;
  case ST_BREAK:
  case ST_CONTINUE:
    if (!c -> loop_depth) type_error(c, s -> at, "loop control used outside a loop");
    if (c -> parallel_depth) type_error(c, s -> at, "break/continue are not allowed in a parallel loop");
    break;
  }
}

void check_program(Program * p, Diagnostics * d, Arena * a) {
  for (Task * t = p -> tasks; t; t = t -> next) {
    for (Task * u = p -> tasks; u != t; u = u -> next)
      if (!strcmp(t -> name, u -> name)) {
        diagnostic(d, t -> at.line, t -> at.col, "name error", "duplicate task name");
        break;
      }
    if (!strcmp(t -> name, "clock_ns") || !strcmp(t -> name, "clock_resolution_ns") || !strcmp(t -> name, "cycles") || !strcmp(t -> name, "cpu_count") || !strcmp(t -> name, "sqrt") || !strcmp(t -> name, "relax") || !strcmp(t -> name, "bench_report") || !strcmp(t -> name, "read_line") || !strcmp(t -> name, "eof") || !strcmp(t -> name, "is_number") || !strcmp(t -> name, "parse_number")) diagnostic(d, t -> at.line, t -> at.col, "name error", "task name is reserved for a builtin");
  }
  Checker c = {
    p,
    d,
    a,
    0,
    0,
    0,
    0,
    0,
    0
  };
  for (Task * t = p -> tasks; t; t = t -> next) {
    Scope root;
    push_scope( & c, & root);
    c.task = t;
    for (Param * q = t -> params; q; q = q -> next) add_symbol( & c, q -> name, q -> type, false, q -> at);
    check_block( & c, t -> body, false);
    if (t -> return_type != TY_VOID && !guarantees_return(t -> body)) diagnostic(d, t -> at.line, t -> at.col, "type error", "non-void task does not return on every path");
    pop_scope( & c);
  }
  Scope root;
  push_scope( & c, & root);
  c.task = NULL;
  if (p -> start) check_block( & c, p -> start, false);
  pop_scope( & c);
}

