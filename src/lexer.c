#include "velyr.h"

static void token_push(TokenVec * v, Token t) {
  if (v -> len == v -> cap) {
    v -> cap = v -> cap ? v -> cap * 2 : 256;
    v -> data = realloc(v -> data, v -> cap * sizeof( * v -> data));
    if (!v -> data) die_oom();
  }
  v -> data[v -> len++] = t;
}

static TokenKind keyword_kind(const char * s, int n) {
  #define KW(text, kind) if (n == (int) sizeof(text) - 1 && memcmp(s, text, sizeof(text) - 1) == 0) return kind
  KW("task", TK_TASK);
  KW("start", TK_START);
  KW("let", TK_LET);
  KW("var", TK_VAR);
  KW("when", TK_WHEN);
  KW("otherwise", TK_OTHERWISE);
  KW("else", TK_OTHERWISE);
  KW("while", TK_WHILE);
  KW("each", TK_EACH);
  KW("in", TK_IN);
  KW("parallel", TK_PARALLEL);
  KW("reduce", TK_REDUCE);
  KW("give", TK_GIVE);
  KW("say", TK_SAY);
  KW("print", TK_PRINT);
  KW("check", TK_CHECK);
  KW("break", TK_BREAK);
  KW("continue", TK_CONTINUE);
  KW("true", TK_TRUE);
  KW("false", TK_FALSE);
  KW("and", TK_AND);
  KW("or", TK_OR);
  KW("not", TK_NOT);
  KW("i64", TK_T_I64);
  KW("f64", TK_T_F64);
  KW("bool", TK_T_BOOL);
  KW("str", TK_T_STR);
  KW("void", TK_T_VOID);
  #undef KW
  return TK_IDENT;
}

void lex_all(const char * src, TokenVec * out, Diagnostics * diag) {
  const char * p = src;
  int line = 1, col = 1;
  #define ADD(k, st, n, ln, cl) token_push(out, (Token) { \
    k, \
    st, \
    n, \
    ln, \
    cl \
  })
  while ( * p) {
    const char * st = p;
    int ln = line, cl = col;
    if ( * p == ' ' || * p == '\r' || * p == '\t') {
      do {
        col += ( * p == '\t') ? 4 : 1;
        p++;
      } while ( * p == ' ' || * p == '\r' || * p == '\t');
      continue;
    }
    if ( * p == '\n') {
      ADD(TK_NEWLINE, p, 1, line, col);
      p++;
      line++;
      col = 1;
      continue;
    }
    if ( * p == '#' || (p[0] == '/' && p[1] == '/')) {
      while ( * p && * p != '\n') {
        p++;
        col++;
      }
      continue;
    }
    if (isalpha((unsigned char) * p) || * p == '_') {
      p++;
      col++;
      while (isalnum((unsigned char) * p) || * p == '_') {
        p++;
        col++;
      }
      int n = (int)(p - st);
      ADD(keyword_kind(st, n), st, n, ln, cl);
      continue;
    }
    if (isdigit((unsigned char) * p)) {
      bool is_float = false;
      while (isdigit((unsigned char) * p) || * p == '_') {
        p++;
        col++;
      }
      if ( * p == '.' && p[1] != '.' && isdigit((unsigned char) p[1])) {
        is_float = true;
        p++;
        col++;
        while (isdigit((unsigned char) * p) || * p == '_') {
          p++;
          col++;
        }
      }
      if ( * p == 'e' || * p == 'E') {
        is_float = true;
        p++;
        col++;
        if ( * p == '+' || * p == '-') {
          p++;
          col++;
        }
        if (!isdigit((unsigned char) * p)) diagnostic(diag, ln, cl, "lex error", "malformed exponent");
        while (isdigit((unsigned char) * p) || * p == '_') {
          p++;
          col++;
        }
      }
      ADD(is_float ? TK_FLOAT : TK_INT, st, (int)(p - st), ln, cl);
      continue;
    }
    if ( * p == '"') {
      p++;
      col++;
      const char * content = p;
      while ( * p && * p != '"') {
        if ( * p == '\n') {
          diagnostic(diag, ln, cl, "lex error", "string cannot cross a line");
          break;
        }
        if ( * p == '\\' && p[1]) {
          p += 2;
          col += 2;
        } else {
          p++;
          col++;
        }
      }
      if ( * p != '"') {
        diagnostic(diag, ln, cl, "lex error", "unterminated string");
        while ( * p && * p != '\n') p++;
      } else {
        ADD(TK_STRING, content, (int)(p - content), ln, cl);
        p++;
        col++;
      }
      continue;
    }
    #define TWO(a, b, k2, k1) case a: if (p[1] == b) { \
      ADD(k2, p, 2, ln, cl); \
      p += 2; \
      col += 2; \
    } else { \
      ADD(k1, p, 1, ln, cl); \
      p++; \
      col++; \
    } \
    break;
    switch ( * p) {
    case '(':
      ADD(TK_LPAREN, p, 1, ln, cl);
      p++;
      col++;
      break;
    case ')':
      ADD(TK_RPAREN, p, 1, ln, cl);
      p++;
      col++;
      break;
    case '{':
      ADD(TK_LBRACE, p, 1, ln, cl);
      p++;
      col++;
      break;
    case '}':
      ADD(TK_RBRACE, p, 1, ln, cl);
      p++;
      col++;
      break;
    case ',':
      ADD(TK_COMMA, p, 1, ln, cl);
      p++;
      col++;
      break;
    case ';':
      ADD(TK_SEMI, p, 1, ln, cl);
      p++;
      col++;
      break;
    case ':':
      if (p[1] == '=') {
        ADD(TK_DECL_EQ, p, 2, ln, cl);
        p += 2;
        col += 2;
      } else {
        ADD(TK_COLON, p, 1, ln, cl);
        p++;
        col++;
      }
      break;
    case '+':
      if (p[1] == '=') {
        ADD(TK_PLUS_EQ, p, 2, ln, cl);
        p += 2;
        col += 2;
      } else {
        ADD(TK_PLUS, p, 1, ln, cl);
        p++;
        col++;
      }
      break;
    case '-':
      if (p[1] == '>') {
        ADD(TK_ARROW, p, 2, ln, cl);
        p += 2;
        col += 2;
      } else if (p[1] == '=') {
        ADD(TK_MINUS_EQ, p, 2, ln, cl);
        p += 2;
        col += 2;
      } else {
        ADD(TK_MINUS, p, 1, ln, cl);
        p++;
        col++;
      }
      break;
    case '*':
      if (p[1] == '=') {
        ADD(TK_STAR_EQ, p, 2, ln, cl);
        p += 2;
        col += 2;
      } else {
        ADD(TK_STAR, p, 1, ln, cl);
        p++;
        col++;
      }
      break;
    case '/':
      if (p[1] == '=') {
        ADD(TK_SLASH_EQ, p, 2, ln, cl);
        p += 2;
        col += 2;
      } else {
        ADD(TK_SLASH, p, 1, ln, cl);
        p++;
        col++;
      }
      break;
    case '%':
      ADD(TK_PERCENT, p, 1, ln, cl);
      p++;
      col++;
      break;
    case '=':
      if (p[1] == '=') {
        ADD(TK_EQ_EQ, p, 2, ln, cl);
        p += 2;
        col += 2;
      } else {
        ADD(TK_EQ, p, 1, ln, cl);
        p++;
        col++;
      }
      break;
    case '!':
      if (p[1] == '=') {
        ADD(TK_BANG_EQ, p, 2, ln, cl);
        p += 2;
        col += 2;
      } else {
        ADD(TK_BANG, p, 1, ln, cl);
        p++;
        col++;
      }
      break;
    case '<':
      if (p[1] == '=') {
        ADD(TK_LT_EQ, p, 2, ln, cl);
        p += 2;
        col += 2;
      } else {
        ADD(TK_LT, p, 1, ln, cl);
        p++;
        col++;
      }
      break;
    case '>':
      if (p[1] == '=') {
        ADD(TK_GT_EQ, p, 2, ln, cl);
        p += 2;
        col += 2;
      } else {
        ADD(TK_GT, p, 1, ln, cl);
        p++;
        col++;
      }
      break;
    case '&':
      if (p[1] == '&') {
        ADD(TK_AND_AND, p, 2, ln, cl);
        p += 2;
        col += 2;
      } else {
        diagnostic(diag, ln, cl, "lex error", "use 'and' or '&&'");
        p++;
        col++;
      }
      break;
    case '|':
      if (p[1] == '|') {
        ADD(TK_OR_OR, p, 2, ln, cl);
        p += 2;
        col += 2;
      } else {
        diagnostic(diag, ln, cl, "lex error", "use 'or' or '||'");
        p++;
        col++;
      }
      break;
    case '.':
      if (p[1] == '.' && p[2] == '=') {
        ADD(TK_RANGE_EQ, p, 3, ln, cl);
        p += 3;
        col += 3;
      } else if (p[1] == '.') {
        ADD(TK_RANGE, p, 2, ln, cl);
        p += 2;
        col += 2;
      } else {
        diagnostic(diag, ln, cl, "lex error", "unexpected '.'");
        p++;
        col++;
      }
      break;
    default: {
      char msg[80];
      snprintf(msg, sizeof(msg), "unexpected character '%c'", * p);
      diagnostic(diag, ln, cl, "lex error", msg);
      p++;
      col++;
    }
    }
    #undef TWO
  }
  ADD(TK_EOF, p, 0, line, col);
  #undef ADD
}
