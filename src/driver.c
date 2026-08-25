#include "velyr.h"

static char g_root[PATH_MAX];
static bool file_exists(const char * p) {
  struct stat st;
  return stat(p, & st) == 0 && S_ISREG(st.st_mode);
}
static void path_dirname(char * p) {
  char * s = strrchr(p, '/');
  if (s) * s = 0;
  else strcpy(p, ".");
}
static bool root_valid(const char * r) {
  char p[PATH_MAX];
  snprintf(p, sizeof(p), "%s/runtime/velyr_rt.c", r);
  return file_exists(p);
}

static void find_root(const char * argv0) {
  const char * env = getenv("VELYR_HOME");
  if (env && root_valid(env)) {
    snprintf(g_root, sizeof(g_root), "%s", env);
    return;
  }
  char exe[PATH_MAX] = {
    0
  };
  #if defined(__linux__)
  ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
  if (n > 0) exe[n] = 0;
  #endif
  if (!exe[0]) snprintf(exe, sizeof(exe), "%s", argv0);
  path_dirname(exe);
  char candidate[PATH_MAX];
  snprintf(candidate, sizeof(candidate), "%s/..", exe);
  if (root_valid(candidate)) {
    snprintf(g_root, sizeof(g_root), "%s", candidate);
    return;
  }
  snprintf(candidate, sizeof(candidate), "%s/../lib/velyr", exe);
  if (root_valid(candidate)) {
    snprintf(g_root, sizeof(g_root), "%s", candidate);
    return;
  }
  if (root_valid(VELYR_BUILD_ROOT)) {
    snprintf(g_root, sizeof(g_root), "%s", VELYR_BUILD_ROOT);
    return;
  }
  snprintf(g_root, sizeof(g_root), ".");
}

static char * read_source(const char * path, size_t * len) {
  FILE * f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "velyr: cannot open %s: %s\n", path, strerror(errno));
    return NULL;
  }
  if (fseek(f, 0, SEEK_END) || ( * len = (size_t) ftell(f), fseek(f, 0, SEEK_SET))) {
    fclose(f);
    return NULL;
  }
  char * s = malloc( * len + 1);
  if (!s) die_oom();
  if (fread(s, 1, * len, f) != * len) {
    fprintf(stderr, "velyr: failed reading %s\n", path);
    free(s);
    fclose(f);
    return NULL;
  }
  s[ * len] = 0;
  fclose(f);
  return s;
}

static int compile_frontend(const char * path,
  const char * cpath, bool check_only) {
  size_t len;
  char * src = read_source(path, & len);
  if (!src) return 1;
  Diagnostics d = {
    path,
    src,
    len,
    0
  };
  TokenVec tv = {
    0
  };
  Arena arena = {
    0
  };
  lex_all(src, & tv, & d);
  if (!d.errors) {
    Parser parser = {
      tv.data,
      tv.len,
      0,
      & arena,
      & d
    };
    Program prog = parse_program( & parser);
    if (!d.errors) check_program( & prog, & d, & arena);
    if (!d.errors && !check_only && !generate_c( & prog, path, cpath)) d.errors++;
  }
  int result = d.errors ? 1 : 0;
  if (!result && check_only) printf("%s: OK\n", path);
  free(tv.data);
  arena_free( & arena);
  free(src);
  return result;
}

static int run_process(char *
  const args[]) {
  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return 1;
  }
  if (pid == 0) {
    execvp(args[0], args);
    fprintf(stderr, "velyr: cannot run %s: %s\n", args[0], strerror(errno));
    _exit(127);
  }
  int status;
  if (waitpid(pid, & status, 0) < 0) {
    perror("waitpid");
    return 1;
  }
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) {
    fprintf(stderr, "velyr: process stopped by signal %d\n", WTERMSIG(status));
    return 128 + WTERMSIG(status);
  }
  return 1;
}

static void default_output(const char * src, char * out, size_t cap) {
  snprintf(out, cap, "%s", src);
  char * slash = strrchr(out, '/');
  char * dot = strrchr(slash ? slash + 1 : out, '.');
  if (dot && strcmp(dot, ".vy") == 0) * dot = 0;
}
static void usage(FILE * f) {
  fprintf(f, "Velyr %s — native AOT compiler\n\nUsage:\n  velyr build <file.vy> [-o binary] [-O0|-O2|-O3] [--portable] [--emit-c file]\n  velyr run   <file.vy> [-- program arguments]\n  velyr check <file.vy>\n  velyr version\n\n", VELYR_VERSION);
}

static int build_command(const char * src,
  const char * out,
    const char * opt, bool portable,
      const char * emit_c, bool quiet) {
  char tmp[PATH_MAX];
  bool keep = emit_c != NULL;
  if (keep) snprintf(tmp, sizeof(tmp), "%s", emit_c);
  else snprintf(tmp, sizeof(tmp), "/tmp/velyr-%ld.c", (long) getpid());
  if (compile_frontend(src, tmp, false)) return 1;
  char include[PATH_MAX * 2], rt[PATH_MAX * 2], asmpath[PATH_MAX * 2];
  snprintf(include, sizeof(include), "-I%s/runtime", g_root);
  snprintf(rt, sizeof(rt), "%s/runtime/velyr_rt.c", g_root);
  #if defined(__x86_64__)
  snprintf(asmpath, sizeof(asmpath), "%s/runtime/x86_64.S", g_root);
  #elif defined(__aarch64__)
  snprintf(asmpath, sizeof(asmpath), "%s/runtime/aarch64.S", g_root);
  #else
  snprintf(asmpath, sizeof(asmpath), "%s/runtime/portable.c", g_root);
  #endif
  if (!file_exists(rt) || !file_exists(asmpath)) {
    fprintf(stderr, "velyr: runtime not found under %s (set VELYR_HOME)\n", g_root);
    if (!keep) unlink(tmp);
    return 1;
  }
  const char * cc = getenv("CC");
  if (!cc || ! * cc) cc = "cc";
  char * args[24];
  int n = 0;
  args[n++] = (char * ) cc;
  args[n++] = "-std=c11";
  args[n++] = (char * ) opt;
  args[n++] = "-DNDEBUG";
  args[n++] = "-fopenmp";
  if (strcmp(opt, "-O0")) {
    args[n++] = "-flto=auto";
    args[n++] = "-fomit-frame-pointer";
    args[n++] = "-fno-plt";
    if (!portable) args[n++] = "-march=native";
  }
  args[n++] = include;
  args[n++] = tmp;
  args[n++] = rt;
  args[n++] = asmpath;
  args[n++] = "-lm";
  args[n++] = "-o";
  args[n++] = (char * ) out;
  args[n] = NULL;
  if (!quiet) printf("AOT  %s -> %s (%s%s)\n", src, out, opt, portable ? ", portable" : " + native CPU");
  int rc = run_process(args);
  if (!keep) unlink(tmp);
  if (!rc && !quiet) printf("DONE %s\n", out);
  return rc;
}

int main(int argc, char ** argv) {
  find_root(argv[0]);
  if (argc < 2) {
    usage(stderr);
    return 2;
  }
  if (!strcmp(argv[1], "version") || !strcmp(argv[1], "--version")) {
    printf("velyr %s\n", VELYR_VERSION);
    return 0;
  }
  if (!strcmp(argv[1], "help") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) {
    usage(stdout);
    return 0;
  }
  if (!strcmp(argv[1], "check")) {
    if (argc != 3) {
      usage(stderr);
      return 2;
    }
    return compile_frontend(argv[2], NULL, true);
  }
  if (!strcmp(argv[1], "build")) {
    if (argc < 3) {
      usage(stderr);
      return 2;
    }
    const char * src = argv[2],
      * output = NULL,
      * emit = NULL,
      * opt = "-O3";
    bool portable = false;
    char def[PATH_MAX];
    for (int i = 3; i < argc; i++) {
      if (!strcmp(argv[i], "-o") && i + 1 < argc) output = argv[++i];
      else if (!strcmp(argv[i], "--emit-c") && i + 1 < argc) emit = argv[++i];
      else if (!strcmp(argv[i], "--portable")) portable = true;
      else if (!strcmp(argv[i], "-O0") || !strcmp(argv[i], "-O2") || !strcmp(argv[i], "-O3")) opt = argv[i];
      else {
        fprintf(stderr, "velyr: unknown option %s\n", argv[i]);
        return 2;
      }
    }
    if (!output) {
      default_output(src, def, sizeof(def));
      output = def;
    }
    return build_command(src, output, opt, portable, emit, false);
  }
  if (!strcmp(argv[1], "run")) {
    if (argc < 3) {
      usage(stderr);
      return 2;
    }
    const char * src = argv[2];
    char out[PATH_MAX];
    snprintf(out, sizeof(out), "/tmp/velyr-run-%ld", (long) getpid());
    int sep = 3;
    while (sep < argc && strcmp(argv[sep], "--")) sep++;
    if (build_command(src, out, "-O3", false, NULL, true)) return 1;
    int extra = argc - (sep < argc ? sep + 1 : argc);
    char ** args = calloc((size_t) extra + 2, sizeof(char * ));
    if (!args) die_oom();
    args[0] = out;
    for (int i = 0; i < extra; i++) args[i + 1] = argv[sep + 1 + i];
    int rc = run_process(args);
    free(args);
    unlink(out);
    return rc;
  }
  fprintf(stderr, "velyr: unknown command '%s'\n", argv[1]);
  usage(stderr);
  return 2;
}
