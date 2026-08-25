# Velyr

**Velyr is a small, speed-first, ahead-of-time compiled language implemented in C.**

It shares scripting's low-friction feel but is built around static types, native AOT output, explicit race-safe reductions, and a compact, modern syntax.

```velyr
task fibonacci(n: i64) -> i64 {
    when n < 2 { give n }
    otherwise  { give fibonacci(n - 1) + fibonacci(n - 2) }
}

start {
    let answer := fibonacci(42)
    say "answer:", answer
}
```

This repository is a working compiler, not a design-only mock-up. Velyr 0.2.1 parses, type-checks, and lowers programs to machine code on every build.

## What is here

| Path | Purpose |
|---|---|
| `src/` | The compiler, split into small modules |
| `runtime/` | Native runtime and a tiny assembly hardware layer |
| `docs/LANGUAGE.md` | The language guide |
| `examples/` | Small runnable programs |
| `benchmarks/` | Reproducible timing harnesses |
| `tests/` | End-to-end test suite |

The compiler source is deliberately modular:

| File | Responsibility |
|---|---|
| `src/velyr.h` | Shared types (arena, diagnostics, token, AST) and cross-module prototypes |
| `src/arena.c` | Bump allocator used throughout the frontend |
| `src/diag.c` | Source-located diagnostics with caret output |
| `src/lexer.c` | Tokenizer |
| `src/parser.c` | Recursive-descent + Pratt expression parser → AST |
| `src/checker.c` | Static semantic analysis and name resolution |
| `src/codegen.c` | Lowering of the typed AST to a C translation unit |
| `src/driver.c` | CLI, build orchestration, and the `main` entry point |

## Build and try it

You need a POSIX environment with GCC or Clang and OpenMP support:

```sh
make
bin/velyr run examples/hello.vy
bin/velyr build examples/fibonacci.vy -o fib
./fib
make test
```

### Command-line usage

```text
velyr build app.vy -o app          optimized for this machine
velyr build app.vy --portable      avoid -march=native
velyr build app.vy -O0             unoptimized debugging build
velyr build app.vy --emit-c app.c  retain the generated C
velyr check app.vy                 parse and type-check only
velyr run app.vy                   AOT-compile to a temporary binary and run
```

`make install PREFIX=/usr/local` installs the compiler and runtime. Set `CC=gcc` or `CC=clang` to pick the native backend.

## What works

- A frontend written from scratch in C: lexer, Pratt parser, AST, static checker, and C emitter.
- Real AOT compilation to a standalone native executable.
- `i64`, `f64`, `bool`, immutable length-aware `str`, and `void`.
- Immutable `let`, mutable `var`, inferred or explicit types.
- Tasks, recursion, `when` / `otherwise`, `while`, ranges, `break`, and `continue`.
- Output via `say` / `print`, and stdin input via `read_line()`, `is_number(s)`, `parse_number(s)`, and `eof()`.
- OpenMP-backed `parallel each` loops with checked `+` / `*` reductions.
- Ahead-of-time diagnostics with source locations and caret underline.
- Native-speed release defaults: `-O3 -flto=auto -march=native -fomit-frame-pointer -fno-plt`.
- Tiny assembly hardware layers for x86-64 and AArch64.
- No GC, VM, bytecode dispatch, JIT warm-up, or runtime heap allocation in the core language.
- No third-party compiler dependency beyond a C compiler and an OpenMP runtime.

## Syntax at a glance

```velyr
task clamp(x: i64, low: i64, high: i64) -> i64 {
    when x < low { give low }
    otherwise when x > high { give high }
    otherwise { give x }
}

start {
    let limit := 100_000_000
    var total: i64 = 0

    parallel each i in 0 ..= limit reduce(+ total) {
        total += i
    }

    say "total:", total
    check total >= 0, "overflow detected"
}
```

See [`docs/LANGUAGE.md`](docs/LANGUAGE.md) for the full language guide.

## AOT pipeline

```text
.vy source
   -> C lexer + parser
   -> typed AST / semantic checks
   -> portable C translation unit
   -> optimizing native C backend + Velyr runtime
   -> ELF/Mach-O native executable
```

Using optimized C as the first native backend is deliberate: it gives Velyr mature register allocation, vectorization, instruction scheduling, LTO, and up-to-date CPU support immediately. The result is still truly ahead-of-time — the output contains no parser, VM, or JIT and runs as native machine code. `--emit-c` makes the intermediate output auditable.

## Assembly policy

Assembly is used only where an exact instruction is the feature:

- x86-64: ordered `RDTSC` and `PAUSE`.
- AArch64: `CNTVCT_EL0` and `YIELD`.

Ordinary arithmetic stays in optimizer-friendly C, and memory movement stays with the compiler/libc, whose CPU-dispatched implementations are generally better than a fixed handwritten loop. Assembly should only be added with reproducible benchmark evidence.

## Performance design

- Static primitive types keep values unboxed.
- Length-aware immutable strings mean printing never scans for a terminator.
- Buffered output and allocation-free integer formatting.
- Static OpenMP scheduling with explicit reduction semantics.
- Native CPU tuning by default, with a portable switch for distribution.
- No hidden exceptions, reference counting, tracing collector, or dynamic dispatch.

Performance claims should be measured on a named machine and workload. [`benchmarks/sum.vy`](benchmarks/sum.vy) is a starting point — it reports hardware counter ticks rather than made-up universal numbers.

## Current scope and next milestones

0.2 is intentionally a usable systems-language kernel, not yet a full replacement for C. The next high-value work is:

1. Fixed-size arrays, slices, bounds-check elimination, and SIMD-friendly bulk operations.
2. Modules and separate compilation.
3. Structs, enums, and explicit ownership for heap values.
4. Profile-guided optimization and richer benchmark harnesses.
5. Windows process/runtime support.
6. An optional direct SSA-to-machine-code backend once the semantics stabilize.

## License

MIT
