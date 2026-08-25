#include "velyr.h"

typedef struct {
  FILE * f;
  int indent, unique;
  const char * source_path;
}
Gen;
static void gi(Gen * g) {
  for (int i = 0; i < g -> indent; i++) fputs("    ", g -> f);
}
static
const char * ctype(Type t) {
  switch (t) {
  case TY_I64:
    return "int64_t";
  case TY_F64:
    return "double";
  case TY_BOOL:
    return "bool";
  case TY_STR:
    return "vy_str";
  case TY_VOID:
    return "void";
  default:
    return "int64_t";
  }
}
static void emit_expr(Gen * g, Expr * e);

static void emit_number(FILE * f,
  const char * s, bool integer) {
  for (;* s; s++)
    if ( * s != '_') fputc( * s, f);
  if (integer) fputs("LL", f);
}
static void emit_c_string(FILE * f,
  const char * s) {
  fputc('"', f);
  for (;* s; s++) {
    unsigned char c = (unsigned char) * s;
    if (c == '"') fputs("\\\"", f);
    else if (c == '\\') {
      fputc('\\', f);
      if (s[1]) fputc( * ++s, f);
    } else if (c < 32 || c >= 127) fprintf(f, "\\x%02x", c);
    else fputc(c, f);
  }
  fputc('"', f);
}
static
const char * cop(TokenKind k) {
  switch (k) {
  case TK_PLUS:
    return "+";
  case TK_MINUS:
    return "-";
  case TK_STAR:
    return "*";
  case TK_SLASH:
    return "/";
  case TK_PERCENT:
    return "%";
  case TK_EQ_EQ:
    return "==";
  case TK_BANG_EQ:
    return "!=";
  case TK_LT:
    return "<";
  case TK_LT_EQ:
    return "<=";
  case TK_GT:
    return ">";
  case TK_GT_EQ:
    return ">=";
  case TK_AND:
  case TK_AND_AND:
    return "&&";
  case TK_OR:
  case TK_OR_OR:
    return "||";
  default:
    return "?";
  }
}

static void emit_expr(Gen * g, Expr * e) {
  switch (e -> kind) {
  case EX_INT:
    emit_number(g -> f, e -> as.text, true);
    break;
  case EX_FLOAT:
    emit_number(g -> f, e -> as.text, false);
    break;
  case EX_STRING:
    fputs("VY_STR_LIT(", g -> f);
    emit_c_string(g -> f, e -> as.text);
    fputc(')', g -> f);
    break;
  case EX_BOOL:
    fputs(e -> as.boolean ? "true" : "false", g -> f);
    break;
  case EX_VAR:
    fprintf(g -> f, "vy_v_%s", e -> as.text);
    break;
  case EX_UNARY:
    fprintf(g -> f, "(%s", (e -> as.unary.op == TK_BANG || e -> as.unary.op == TK_NOT) ? "!" : e -> as.unary.op == TK_MINUS ? "-" : "+");
    emit_expr(g, e -> as.unary.right);
    fputc(')', g -> f);
    break;
  case EX_BINARY:
    if ((e -> as.binary.op == TK_EQ_EQ || e -> as.binary.op == TK_BANG_EQ) && e -> as.binary.left -> type == TY_STR) {
      if (e -> as.binary.op == TK_BANG_EQ) fputc('!', g -> f);
      fputs("vy_str_equal(", g -> f);
      emit_expr(g, e -> as.binary.left);
      fputs(", ", g -> f);
      emit_expr(g, e -> as.binary.right);
      fputc(')', g -> f);
    } else {
      fputc('(', g -> f);
      emit_expr(g, e -> as.binary.left);
      fprintf(g -> f, " %s ", cop(e -> as.binary.op));
      emit_expr(g, e -> as.binary.right);
      fputc(')', g -> f);
    }
    break;
  case EX_CALL: {
    const char * n = e -> as.call.name;
    if (!strcmp(n, "clock_ns")) fputs("vy_clock_ns", g -> f);
    else if (!strcmp(n, "clock_resolution_ns")) fputs("vy_clock_resolution_ns", g -> f);
    else if (!strcmp(n, "cycles")) fputs("vy_cycles", g -> f);
    else if (!strcmp(n, "cpu_count")) fputs("vy_cpu_count", g -> f);
    else if (!strcmp(n, "sqrt")) fputs("sqrt", g -> f);
    else if (!strcmp(n, "relax")) fputs("vy_cpu_relax", g -> f);
    else if (!strcmp(n, "bench_report")) fputs("vy_bench_report", g -> f);
    else if (!strcmp(n, "read_line")) fputs("vy_read_line", g -> f);
    else if (!strcmp(n, "eof")) fputs("vy_eof", g -> f);
    else if (!strcmp(n, "is_number")) fputs("vy_is_number", g -> f);
    else if (!strcmp(n, "parse_number")) fputs("vy_parse_number", g -> f);
    else fprintf(g -> f, "vy_fn_%s", n);
    fputc('(', g -> f);
    for (ExprList * a = e -> as.call.args; a; a = a -> next) {
      emit_expr(g, a -> expr);
      if (a -> next) fputs(", ", g -> f);
    }
    fputc(')', g -> f);
    break;
  }
  }
}

static void emit_stmt(Gen * g, Stmt * s);
static void emit_block(Gen * g, Stmt * b) {
  fputs("{\n", g -> f);
  g -> indent++;
  for (Stmt * s = b -> as.block.first; s; s = s -> next) emit_stmt(g, s);
  g -> indent--;
  gi(g);
  fputc('}', g -> f);
}
static
const char * assignop(TokenKind k) {
  switch (k) {
  case TK_EQ:
    return "=";
  case TK_PLUS_EQ:
    return "+=";
  case TK_MINUS_EQ:
    return "-=";
  case TK_STAR_EQ:
    return "*=";
  case TK_SLASH_EQ:
    return "/=";
  default:
    return "=";
  }
}
static void emit_print_value(Gen * g, Expr * e) {
  const char * n = e -> type == TY_I64 ? "vy_print_i64" : e -> type == TY_F64 ? "vy_print_f64" : e -> type == TY_BOOL ? "vy_print_bool" : "vy_print_str";
  fprintf(g -> f, "%s(", n);
  emit_expr(g, e);
  fputs(");\n", g -> f);
}

static void emit_stmt(Gen * g, Stmt * s) {
  switch (s -> kind) {
  case ST_BLOCK:
    gi(g);
    emit_block(g, s);
    fputc('\n', g -> f);
    break;
  case ST_DECL:
    gi(g);
    if (!s -> as.decl.mut) fputs("const ", g -> f);
    fprintf(g -> f, "%s vy_v_%s = ", ctype(s -> as.decl.declared), s -> as.decl.name);
    emit_expr(g, s -> as.decl.init);
    fputs(";\n", g -> f);
    break;
  case ST_ASSIGN:
    gi(g);
    fprintf(g -> f, "vy_v_%s %s ", s -> as.assign.name, assignop(s -> as.assign.op));
    emit_expr(g, s -> as.assign.value);
    fputs(";\n", g -> f);
    break;
  case ST_EXPR:
    gi(g);
    emit_expr(g, s -> as.expr.expr);
    fputs(";\n", g -> f);
    break;
  case ST_IF:
    gi(g);
    fputs("if (", g -> f);
    emit_expr(g, s -> as.ifs.cond);
    fputs(") ", g -> f);
    emit_block(g, s -> as.ifs.then_branch);
    if (s -> as.ifs.else_branch) {
      fputs(" else ", g -> f);
      if (s -> as.ifs.else_branch -> kind == ST_IF) {
        fputs("\n", g -> f);
        emit_stmt(g, s -> as.ifs.else_branch);
        return;
      }
      emit_block(g, s -> as.ifs.else_branch);
    }
    fputc('\n', g -> f);
    break;
  case ST_WHILE:
    gi(g);
    fputs("while (", g -> f);
    emit_expr(g, s -> as.whiles.cond);
    fputs(") ", g -> f);
    emit_block(g, s -> as.whiles.body);
    fputc('\n', g -> f);
    break;
  case ST_FOR: {
    int id = g -> unique++;
    gi(g);
    fprintf(g -> f, "const int64_t vy_begin_%d = ", id);
    emit_expr(g, s -> as.fors.from);
    fputs(";\n", g -> f);
    gi(g);
    fprintf(g -> f, "const int64_t vy_end_%d = ", id);
    emit_expr(g, s -> as.fors.to);
    fputs(";\n", g -> f);
    if (s -> as.fors.parallel) {
      gi(g);
      if (s -> as.fors.reduction) fprintf(g -> f, "#pragma omp parallel for schedule(static) reduction(%c:vy_v_%s)\n", s -> as.fors.reduce_op == TK_STAR ? '*' : '+', s -> as.fors.reduction);
      else fputs("#pragma omp parallel for schedule(static)\n", g -> f);
    }
    gi(g);
    fprintf(g -> f, "for (int64_t vy_v_%s = vy_begin_%d; vy_v_%s %s vy_end_%d; ++vy_v_%s) ", s -> as.fors.name, id, s -> as.fors.name, s -> as.fors.inclusive ? "<=" : "<", id, s -> as.fors.name);
    emit_block(g, s -> as.fors.body);
    fputc('\n', g -> f);
    break;
  }
  case ST_RETURN:
    gi(g);
    if (s -> as.returns.value) {
      fputs("return ", g -> f);
      emit_expr(g, s -> as.returns.value);
      fputs(";\n", g -> f);
    } else fputs("return;\n", g -> f);
    break;
  case ST_SAY:
    gi(g);
    fputs("vy_out_lock();\n", g -> f);
    for (ExprList * x = s -> as.says.values; x; x = x -> next) {
      gi(g);
      emit_print_value(g, x -> expr);
      if (x -> next) {
        gi(g);
        fputs("vy_print_space();\n", g -> f);
      }
    }
    if (s -> as.says.newline) {
      gi(g);
      fputs("vy_print_newline();\n", g -> f);
    }
    gi(g);
    fputs("vy_out_unlock();\n", g -> f);
    break;
  case ST_CHECK:
    gi(g);
    fputs("if (!(", g -> f);
    emit_expr(g, s -> as.checks.cond);
    fputs(")) vy_panic(", g -> f);
    if (s -> as.checks.message) emit_expr(g, s -> as.checks.message);
    else fputs("VY_STR_LIT(\"check failed\")", g -> f);
    fputs(", VY_STR_LIT(", g -> f);
    emit_c_string(g -> f, g -> source_path);
    fprintf(g -> f, "), %d);\n", s -> at.line);
    break;
  case ST_BREAK:
    gi(g);
    fputs("break;\n", g -> f);
    break;
  case ST_CONTINUE:
    gi(g);
    fputs("continue;\n", g -> f);
    break;
  }
}

static void emit_params(FILE * f, Param * p) {
  for (; p; p = p -> next) {
    fprintf(f, "%s vy_v_%s", ctype(p -> type), p -> name);
    if (p -> next) fputs(", ", f);
  }
}
bool generate_c(Program * p,
  const char * source_path,
    const char * outpath) {
  FILE * f = fopen(outpath, "wb");
  if (!f) {
    fprintf(stderr, "velyr: cannot create %s: %s\n", outpath, strerror(errno));
    return false;
  }
  fprintf(f, "/* Generated by Velyr %s. Native AOT translation unit. */\n#include \"velyr_rt.h\"\n#include <math.h>\n\n", VELYR_VERSION);
  for (Task * t = p -> tasks; t; t = t -> next) {
    fprintf(f, "static %s vy_fn_%s(", ctype(t -> return_type), t -> name);
    emit_params(f, t -> params);
    fputs(");\n", f);
  }
  fputc('\n', f);
  Gen g = {
    f,
    0,
    0,
    source_path
  };
  for (Task * t = p -> tasks; t; t = t -> next) {
    fprintf(f, "static %s vy_fn_%s(", ctype(t -> return_type), t -> name);
    emit_params(f, t -> params);
    fputs(") ", f);
    emit_block( & g, t -> body);
    fputs("\n\n", f);
  }
  fputs("int main(void) {\n    vy_runtime_init();\n", f);
  g.indent = 1;
  for (Stmt * s = p -> start -> as.block.first; s; s = s -> next) emit_stmt( & g, s);
  fputs("    vy_runtime_shutdown();\n    return 0;\n}\n", f);
  if (fclose(f) != 0) {
    fprintf(stderr, "velyr: failed writing %s\n", outpath);
    return false;
  }
  return true;
}

