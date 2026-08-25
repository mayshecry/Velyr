#include "velyr.h"

static Token * pk(Parser * p) {
  return & p -> tokens[p -> pos];
}
static Token * prev(Parser * p) {
  return & p -> tokens[p -> pos - 1];
}
static bool is(Parser * p, TokenKind k) {
  return pk(p) -> kind == k;
}
static Token take(Parser * p) {
  if (p -> pos < p -> count) return p -> tokens[p -> pos++];
  return p -> tokens[p -> count - 1];
}
static bool match(Parser * p, TokenKind k) {
  if (is(p, k)) {
    p -> pos++;
    return true;
  }
  return false;
}
static void skip_newlines(Parser * p) {
  while (match(p, TK_NEWLINE)) {}
}
static void skip_lines(Parser * p) {
  while (match(p, TK_NEWLINE) || match(p, TK_SEMI)) {}
}

static
const char * token_name(TokenKind k) {
  switch (k) {
  case TK_IDENT:
    return "name";
  case TK_LPAREN:
    return "'('";
  case TK_RPAREN:
    return "')'";
  case TK_LBRACE:
    return "'{'";
  case TK_RBRACE:
    return "'}'";
  case TK_COLON:
    return "':'";
  case TK_COMMA:
    return "','";
  case TK_ARROW:
    return "'->'";
  case TK_EQ:
    return "'='";
  case TK_IN:
    return "'in'";
  case TK_EACH:
    return "'each'";
  case TK_EOF:
    return "end of file";
  default:
    return "token";
  }
}

static Token expect(Parser * p, TokenKind k,
  const char * context) {
  if (is(p, k)) return take(p);
  char msg[160];
  snprintf(msg, sizeof(msg), "expected %s %s, found '%.*s'", token_name(k), context, pk(p) -> len, pk(p) -> start);
  diagnostic(p -> diag, pk(p) -> line, pk(p) -> col, "syntax error", msg);
  return (Token) {
    k,
    "",
    0,
    pk(p) -> line,
    pk(p) -> col
  };
}

static char * tok_text(Parser * p, Token t) {
  return arena_strndup(p -> arena, t.start, (size_t) t.len);
}
static Expr * new_expr(Parser * p, ExprKind k, Token at) {
  Expr * e = arena_alloc(p -> arena, sizeof( * e));
  e -> kind = k;
  e -> at = at;
  return e;
}
static Stmt * new_stmt(Parser * p, StmtKind k, Token at) {
  Stmt * s = arena_alloc(p -> arena, sizeof( * s));
  s -> kind = k;
  s -> at = at;
  return s;
}

static Type parse_type(Parser * p) {
  Token t = take(p);
  switch (t.kind) {
  case TK_T_I64:
    return TY_I64;
  case TK_T_F64:
    return TY_F64;
  case TK_T_BOOL:
    return TY_BOOL;
  case TK_T_STR:
    return TY_STR;
  case TK_T_VOID:
    return TY_VOID;
  default:
    diagnostic(p -> diag, t.line, t.col, "syntax error", "expected a type: i64, f64, bool, str, or void");
    return TY_UNKNOWN;
  }
}

static int precedence(TokenKind k) {
  switch (k) {
  case TK_OR:
  case TK_OR_OR:
    return 1;
  case TK_AND:
  case TK_AND_AND:
    return 2;
  case TK_EQ_EQ:
  case TK_BANG_EQ:
    return 3;
  case TK_LT:
  case TK_LT_EQ:
  case TK_GT:
  case TK_GT_EQ:
    return 4;
  case TK_PLUS:
  case TK_MINUS:
    return 5;
  case TK_STAR:
  case TK_SLASH:
  case TK_PERCENT:
    return 6;
  default:
    return 0;
  }
}

static Expr * parse_expr_prec(Parser * p, int min_prec);

static Expr * parse_primary(Parser * p) {
  Token t = take(p);
  switch (t.kind) {
  case TK_INT: {
    Expr * e = new_expr(p, EX_INT, t);
    e -> as.text = tok_text(p, t);
    return e;
  }
  case TK_FLOAT: {
    Expr * e = new_expr(p, EX_FLOAT, t);
    e -> as.text = tok_text(p, t);
    return e;
  }
  case TK_STRING: {
    Expr * e = new_expr(p, EX_STRING, t);
    e -> as.text = tok_text(p, t);
    return e;
  }
  case TK_TRUE:
  case TK_FALSE: {
    Expr * e = new_expr(p, EX_BOOL, t);
    e -> as.boolean = t.kind == TK_TRUE;
    return e;
  }
  case TK_IDENT: {
    char * name = tok_text(p, t);
    if (match(p, TK_LPAREN)) {
      Expr * e = new_expr(p, EX_CALL, t);
      e -> as.call.name = name;
      ExprList ** tail = & e -> as.call.args;
      skip_newlines(p);
      while (!is(p, TK_RPAREN) && !is(p, TK_EOF)) {
        ExprList * item = arena_alloc(p -> arena, sizeof( * item));
        item -> expr = parse_expr_prec(p, 1);* tail = item;
        tail = & item -> next;
        e -> as.call.argc++;
        skip_newlines(p);
        if (!match(p, TK_COMMA)) break;
        skip_newlines(p);
        if (is(p, TK_RPAREN)) break;
      }
      expect(p, TK_RPAREN, "after call arguments");
      return e;
    }
    Expr * e = new_expr(p, EX_VAR, t);
    e -> as.text = name;
    return e;
  }
  case TK_LPAREN: {
    skip_newlines(p);
    Expr * e = parse_expr_prec(p, 1);
    skip_newlines(p);
    expect(p, TK_RPAREN, "after expression");
    return e;
  }
  default:
    diagnostic(p -> diag, t.line, t.col, "syntax error", "expected an expression");
    {
      Expr * e = new_expr(p, EX_INT, t);
      e -> as.text = "0";
      return e;
    }
  }
}

static Expr * parse_unary(Parser * p) {
  if (is(p, TK_MINUS) || is(p, TK_BANG) || is(p, TK_NOT) || is(p, TK_PLUS)) {
    Token op = take(p);
    Expr * e = new_expr(p, EX_UNARY, op);
    e -> as.unary.op = op.kind;
    e -> as.unary.right = parse_unary(p);
    return e;
  }
  return parse_primary(p);
}

static Expr * parse_expr_prec(Parser * p, int min_prec) {
  Expr * left = parse_unary(p);
  while (precedence(pk(p) -> kind) >= min_prec) {
    Token op = take(p);
    int prec = precedence(op.kind);
    Expr * right = parse_expr_prec(p, prec + 1);
    Expr * e = new_expr(p, EX_BINARY, op);
    e -> as.binary.op = op.kind;
    e -> as.binary.left = left;
    e -> as.binary.right = right;
    left = e;
  }
  return left;
}
static Expr * parse_expr(Parser * p) {
  return parse_expr_prec(p, 1);
}

static void statement_end(Parser * p) {
  if (match(p, TK_SEMI)) {
    while (match(p, TK_SEMI) || match(p, TK_NEWLINE)) {}
    return;
  }
  if (match(p, TK_NEWLINE)) {
    while (match(p, TK_NEWLINE) || match(p, TK_SEMI)) {}
    return;
  }
  if (is(p, TK_RBRACE) || is(p, TK_EOF)) return;
  diagnostic(p -> diag, pk(p) -> line, pk(p) -> col, "syntax error", "expected a newline, ';', or '}' after statement");
  while (!is(p, TK_NEWLINE) && !is(p, TK_RBRACE) && !is(p, TK_EOF)) take(p);
  skip_lines(p);
}

static Stmt * parse_statement(Parser * p);

static Stmt * parse_block(Parser * p) {
  Token at = expect(p, TK_LBRACE, "to begin a block");
  Stmt * block = new_stmt(p, ST_BLOCK, at), ** tail = & block -> as.block.first;
  skip_lines(p);
  while (!is(p, TK_RBRACE) && !is(p, TK_EOF)) {
    Stmt * s = parse_statement(p);
    if (s) {
      * tail = s;
      while ( * tail) tail = & ( * tail) -> next;
    }
    skip_lines(p);
  }
  expect(p, TK_RBRACE, "to close the block");
  return block;
}

static Stmt * parse_decl(Parser * p, bool mut, Token at) {
  Token name = expect(p, TK_IDENT, "after declaration keyword");
  Type declared = TY_UNKNOWN;
  if (match(p, TK_COLON)) declared = parse_type(p);
  if (!match(p, TK_DECL_EQ) && !match(p, TK_EQ)) expect(p, TK_EQ, "before the initial value");
  Expr * init = parse_expr(p);
  Stmt * s = new_stmt(p, ST_DECL, at);
  s -> as.decl.name = tok_text(p, name);
  s -> as.decl.declared = declared;
  s -> as.decl.init = init;
  s -> as.decl.mut = mut;
  statement_end(p);
  return s;
}

static Stmt * parse_if(Parser * p, Token at) {
  Expr * cond = parse_expr(p);
  skip_lines(p);
  Stmt * thenb = parse_block(p), * elseb = NULL;
  size_t save = p -> pos;
  skip_lines(p);
  if (match(p, TK_OTHERWISE)) {
    skip_lines(p);
    if (match(p, TK_WHEN)) elseb = parse_if(p, * prev(p));
    else elseb = parse_block(p);
  } else p -> pos = save;
  Stmt * s = new_stmt(p, ST_IF, at);
  s -> as.ifs.cond = cond;
  s -> as.ifs.then_branch = thenb;
  s -> as.ifs.else_branch = elseb;
  return s;
}

static Stmt * parse_for(Parser * p, Token at, bool parallel) {
  expect(p, TK_EACH, "after 'parallel'");
  Token name = expect(p, TK_IDENT, "as the loop index");
  expect(p, TK_IN, "after the loop index");
  Expr * from = parse_expr(p);
  bool inclusive = false;
  if (match(p, TK_RANGE_EQ)) inclusive = true;
  else expect(p, TK_RANGE, "between loop bounds");
  Expr * to = parse_expr(p);
  char * reduction = NULL;
  TokenKind rop = TK_ERROR;
  if (match(p, TK_REDUCE)) {
    expect(p, TK_LPAREN, "after 'reduce'");
    Token op = take(p);
    if (op.kind != TK_PLUS && op.kind != TK_STAR) diagnostic(p -> diag, op.line, op.col, "syntax error", "reduction operator must be '+' or '*'");
    rop = op.kind;
    Token r = expect(p, TK_IDENT, "as reduction variable");
    reduction = tok_text(p, r);
    expect(p, TK_RPAREN, "after reduction");
  }
  skip_lines(p);
  Stmt * body = parse_block(p);
  Stmt * s = new_stmt(p, ST_FOR, at);
  s -> as.fors.name = tok_text(p, name);
  s -> as.fors.from = from;
  s -> as.fors.to = to;
  s -> as.fors.inclusive = inclusive;
  s -> as.fors.parallel = parallel;
  s -> as.fors.reduction = reduction;
  s -> as.fors.reduce_op = rop;
  s -> as.fors.body = body;
  return s;
}

static Stmt * parse_say(Parser * p, Token at, bool newline) {
  Stmt * s = new_stmt(p, ST_SAY, at);
  s -> as.says.newline = newline;
  ExprList ** tail = & s -> as.says.values;
  if (!is(p, TK_NEWLINE) && !is(p, TK_SEMI) && !is(p, TK_RBRACE)) {
    do {
      ExprList * i = arena_alloc(p -> arena, sizeof( * i));
      i -> expr = parse_expr(p);* tail = i;
      tail = & i -> next;
    } while (match(p, TK_COMMA));
  }
  statement_end(p);
  return s;
}

static Stmt * parse_statement(Parser * p) {
  skip_lines(p);
  Token at = * pk(p);
  if (match(p, TK_LET)) return parse_decl(p, false, at);
  if (match(p, TK_VAR)) return parse_decl(p, true, at);
  if (match(p, TK_WHEN)) return parse_if(p, at);
  if (match(p, TK_WHILE)) {
    Expr * cond = parse_expr(p);
    skip_lines(p);
    Stmt * body = parse_block(p);
    Stmt * s = new_stmt(p, ST_WHILE, at);
    s -> as.whiles.cond = cond;
    s -> as.whiles.body = body;
    return s;
  }
  if (match(p, TK_EACH)) {
    p -> pos--;
    return parse_for(p, at, false);
  }
  if (match(p, TK_PARALLEL)) return parse_for(p, at, true);
  if (match(p, TK_GIVE)) {
    Stmt * s = new_stmt(p, ST_RETURN, at);
    if (!is(p, TK_NEWLINE) && !is(p, TK_SEMI) && !is(p, TK_RBRACE)) s -> as.returns.value = parse_expr(p);
    statement_end(p);
    return s;
  }
  if (match(p, TK_SAY)) return parse_say(p, at, true);
  if (match(p, TK_PRINT)) return parse_say(p, at, false);
  if (match(p, TK_CHECK)) {
    Stmt * s = new_stmt(p, ST_CHECK, at);
    s -> as.checks.cond = parse_expr(p);
    if (match(p, TK_COMMA)) s -> as.checks.message = parse_expr(p);
    statement_end(p);
    return s;
  }
  if (match(p, TK_BREAK)) {
    Stmt * s = new_stmt(p, ST_BREAK, at);
    statement_end(p);
    return s;
  }
  if (match(p, TK_CONTINUE)) {
    Stmt * s = new_stmt(p, ST_CONTINUE, at);
    statement_end(p);
    return s;
  }
  if (is(p, TK_LBRACE)) return parse_block(p);

  Expr * left = parse_expr(p);
  if (is(p, TK_EQ) || is(p, TK_PLUS_EQ) || is(p, TK_MINUS_EQ) || is(p, TK_STAR_EQ) || is(p, TK_SLASH_EQ)) {
    Token op = take(p);
    Stmt * s = new_stmt(p, ST_ASSIGN, at);
    if (left -> kind != EX_VAR) diagnostic(p -> diag, left -> at.line, left -> at.col, "syntax error", "assignment target must be a variable");
    s -> as.assign.name = left -> kind == EX_VAR ? left -> as.text : "_error";
    s -> as.assign.op = op.kind;
    s -> as.assign.value = parse_expr(p);
    statement_end(p);
    return s;
  }
  Stmt * s = new_stmt(p, ST_EXPR, at);
  s -> as.expr.expr = left;
  statement_end(p);
  return s;
}

static Task * parse_task(Parser * p, Token at) {
  Token name = expect(p, TK_IDENT, "after 'task'");
  Task * t = arena_alloc(p -> arena, sizeof( * t));
  t -> name = tok_text(p, name);
  t -> at = at;
  expect(p, TK_LPAREN, "after task name");
  Param ** tail = & t -> params;
  skip_newlines(p);
  while (!is(p, TK_RPAREN) && !is(p, TK_EOF)) {
    Token pn = expect(p, TK_IDENT, "as parameter name");
    expect(p, TK_COLON, "after parameter name");
    Type ty = parse_type(p);
    if (ty == TY_VOID) diagnostic(p -> diag, pn.line, pn.col, "type error", "parameter cannot have type void");
    Param * q = arena_alloc(p -> arena, sizeof( * q));
    q -> name = tok_text(p, pn);
    q -> type = ty;
    q -> at = pn;* tail = q;
    tail = & q -> next;
    t -> param_count++;
    skip_newlines(p);
    if (!match(p, TK_COMMA)) break;
    skip_newlines(p);
    if (is(p, TK_RPAREN)) break;
  }
  expect(p, TK_RPAREN, "after parameters");
  t -> return_type = TY_VOID;
  if (match(p, TK_ARROW)) t -> return_type = parse_type(p);
  skip_lines(p);
  t -> body = parse_block(p);
  return t;
}

Program parse_program(Parser * p) {
  Program prog = {
    0
  };
  Task ** tail = & prog.tasks;
  skip_lines(p);
  while (!is(p, TK_EOF)) {
    Token at = * pk(p);
    if (match(p, TK_TASK)) {
      Task * t = parse_task(p, at);* tail = t;
      tail = & t -> next;
    } else if (match(p, TK_START)) {
      if (prog.start) diagnostic(p -> diag, at.line, at.col, "syntax error", "only one start block is allowed");
      skip_lines(p);
      prog.start = parse_block(p);
    } else {
      diagnostic(p -> diag, at.line, at.col, "syntax error", "top level accepts only 'task' and 'start'");
      while (!is(p, TK_NEWLINE) && !is(p, TK_EOF)) take(p);
    }
    skip_lines(p);
  }
  if (!prog.start) diagnostic(p -> diag, pk(p) -> line, pk(p) -> col, "syntax error", "program needs a start { ... } block");
  return prog;
}

