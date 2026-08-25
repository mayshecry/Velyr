#ifndef VELYR_H
#define VELYR_H

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>

#include <errno.h>

#include <limits.h>

#include <stdbool.h>

#include <stdint.h>

#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include <sys/stat.h>

#include <sys/types.h>

#include <sys/wait.h>

#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef VELYR_BUILD_ROOT
#define VELYR_BUILD_ROOT "."
#endif

#define VELYR_VERSION "0.2.1"

typedef struct ArenaChunk {
  struct ArenaChunk * next;
  size_t used, cap;
  unsigned char data[];
}
ArenaChunk;

typedef struct {
  ArenaChunk * head;
}
Arena;

typedef struct {
  const char * path;
  const char * source;
  size_t source_len;
  int errors;
}
Diagnostics;

typedef enum {
  TK_EOF,
  TK_ERROR,
  TK_NEWLINE,
  TK_IDENT,
  TK_INT,
  TK_FLOAT,
  TK_STRING,
  TK_LPAREN,
  TK_RPAREN,
  TK_LBRACE,
  TK_RBRACE,
  TK_COMMA,
  TK_COLON,
  TK_SEMI,
  TK_PLUS,
  TK_MINUS,
  TK_STAR,
  TK_SLASH,
  TK_PERCENT,
  TK_EQ,
  TK_DECL_EQ,
  TK_PLUS_EQ,
  TK_MINUS_EQ,
  TK_STAR_EQ,
  TK_SLASH_EQ,
  TK_EQ_EQ,
  TK_BANG_EQ,
  TK_LT,
  TK_LT_EQ,
  TK_GT,
  TK_GT_EQ,
  TK_BANG,
  TK_AND_AND,
  TK_OR_OR,
  TK_ARROW,
  TK_RANGE,
  TK_RANGE_EQ,
  TK_TASK,
  TK_START,
  TK_LET,
  TK_VAR,
  TK_WHEN,
  TK_OTHERWISE,
  TK_WHILE,
  TK_EACH,
  TK_IN,
  TK_PARALLEL,
  TK_REDUCE,
  TK_GIVE,
  TK_SAY,
  TK_PRINT,
  TK_CHECK,
  TK_BREAK,
  TK_CONTINUE,
  TK_TRUE,
  TK_FALSE,
  TK_AND,
  TK_OR,
  TK_NOT,
  TK_T_I64,
  TK_T_F64,
  TK_T_BOOL,
  TK_T_STR,
  TK_T_VOID
}
TokenKind;

typedef struct {
  TokenKind kind;
  const char * start;
  int len, line, col;
}
Token;

typedef struct {
  Token * data;
  size_t len, cap;
}
TokenVec;

typedef enum {
  TY_UNKNOWN,
  TY_VOID,
  TY_I64,
  TY_F64,
  TY_BOOL,
  TY_STR
}
Type;
typedef enum {
  EX_INT,
  EX_FLOAT,
  EX_STRING,
  EX_BOOL,
  EX_VAR,
  EX_UNARY,
  EX_BINARY,
  EX_CALL
}
ExprKind;

typedef struct Expr Expr;
typedef struct ExprList {
  Expr * expr;
  struct ExprList * next;
}
ExprList;

struct Expr {
  ExprKind kind;
  Token at;
  Type type;
  union {
    char * text;
    bool boolean;
    struct {
      TokenKind op;
      Expr * right;
    }
    unary;
    struct {
      TokenKind op;
      Expr * left, * right;
    }
    binary;
    struct {
      char * name;
      ExprList * args;
      int argc;
    }
    call;
  }
  as;
};

typedef enum {
  ST_BLOCK,
  ST_DECL,
  ST_ASSIGN,
  ST_EXPR,
  ST_IF,
  ST_WHILE,
  ST_FOR,
  ST_RETURN,
  ST_SAY,
  ST_CHECK,
  ST_BREAK,
  ST_CONTINUE
}
StmtKind;

typedef struct Stmt Stmt;
struct Stmt {
  StmtKind kind;
  Token at;
  Stmt * next;
  union {
    struct {
      Stmt * first;
    }
    block;
    struct {
      char * name;
      Type declared;
      Expr * init;
      bool mut;
    }
    decl;
    struct {
      char * name;
      TokenKind op;
      Expr * value;
    }
    assign;
    struct {
      Expr * expr;
    }
    expr;
    struct {
      Expr * cond;
      Stmt * then_branch, * else_branch;
    }
    ifs;
    struct {
      Expr * cond;
      Stmt * body;
    }
    whiles;
    struct {
      char * name;
      Expr * from, * to;
      bool inclusive, parallel;
      char * reduction;
      TokenKind reduce_op;
      Stmt * body;
    }
    fors;
    struct {
      Expr * value;
    }
    returns;
    struct {
      ExprList * values;
      bool newline;
    }
    says;
    struct {
      Expr * cond;
      Expr * message;
    }
    checks;
  }
  as;
};

typedef struct Param {
  char * name;
  Type type;
  Token at;
  struct Param * next;
}
Param;
typedef struct Task {
  char * name;
  Token at;
  Param * params;
  int param_count;
  Type return_type;
  Stmt * body;
  struct Task * next;
}
Task;

typedef struct {
  Task * tasks;
  Stmt * start;
}
Program;

typedef struct {
  Token * tokens;
  size_t count, pos;
  Arena * arena;
  Diagnostics * diag;
}
Parser;

void die_oom(void);
void * arena_alloc(Arena * a, size_t n);
char * arena_strndup(Arena * a, const char * s, size_t n);
void arena_free(Arena * a);

void diagnostic(Diagnostics * d, int line, int col, const char * kind, const char * msg);

void lex_all(const char * src, TokenVec * out, Diagnostics * diag);

Program parse_program(Parser * p);

void check_program(Program * p, Diagnostics * d, Arena * a);

bool generate_c(Program * p, const char * source_path, const char * outpath);

#endif
