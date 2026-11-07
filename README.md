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

## Networking

Velyr exposes POSIX sockets directly, so programs can do real network I/O at native speed:

| Built-in | Meaning |
|---|---|
| `net_af_inet()` / `net_af_inet6()` | address family constants |
| `net_sock_stream()` / `net_sock_dgram()` / `net_sock_raw()` | socket type constants |
| `net_ipproto_icmp()` / `net_ipproto_tcp()` / `net_ipproto_udp()` | protocol constants |
| `net_socket(domain, type, protocol)` | socket → fd as `i64` |
| `net_connect(fd, addr, port)` / `net_bind(fd, port)` / `net_listen(fd, backlog)` | TCP/UDP setup, returns `bool` |
| `net_accept(fd)` | accept → client fd as `i64` |
| `net_send(fd, data)` / `net_recv(fd, max_len)` | stream send/recv |
| `net_sendto(fd, data, addr, port)` / `net_recvfrom(fd, max_len)` | datagram send/recv |
| `net_close(fd)` | close a socket |
| `net_setblocking(fd, bool)` / `net_settimeout(fd, ms)` | blocking mode and timeouts |
| `net_setsockopt(fd, level, optname, value)` | socket options, returns `bool` |
| `net_resolve(host)` | hostname → IPv4 `str` |
| `net_ping_once(host, timeout_ms, size)` | ICMP echo round-trip in nanoseconds, or `-1` on timeout/error |
| `net_ping_fd(fd, addr, port, size)` | ICMP echo on an already-open raw socket, round-trip nanoseconds, or `-1` (no socket creation per call — lowest overhead) |
| `net_sendmmsg_batch(fd, data, addr, port, count)` | Send up to `count` UDP datagrams in a single `sendmmsg()` syscall; returns packets sent |
| `net_sol_socket()` | `SOL_SOCKET` constant |
| `net_sndbuf()` | `SO_SNDBUF` constant |

## Command-line arguments

| Built-in | Result |
|---|---|
| `arg_count()` | `i64`, number of arguments after the program name |
| `arg(i)` | `str`, i-th argument (arg(0) is the first) |
| `parse_int(s)` | `i64`, parse a string as an integer |

Example — UDP flooder (`examples/hi.vy`):

```velyr
start {
    when arg_count() >= 3 {
        let ip := arg(0)
        let port := parse_int(arg(1))
        let duration_s := parse_int(arg(2))
        flood(ip, port, duration_s)
    } otherwise {
        say "usage: hi IP PORT TIME"
    }
}
```

`net_ping_once` / `net_ping_fd` open a raw `ICMP` socket, so they require `CAP_NET_RAW` (typically root). Everything else works with ordinary user privileges.

`examples/hyper_ping.vy` is the hardware-close pinger: one persistent raw socket, zero inter-packet delay, `clock_ns()` timing, min/worst/avg RTT and loss reporting. Give it an IP on stdin and it fires as fast as the kernel and NIC allow.

```velyr
start {
    print "ip: "
    let host := read_line()
    ...
    each i in 0 .. count {
        sent += 1
        let r := net_ping_fd(fd, host, 0, size)
        ...
    }
}
```

Example:

```velyr
start {
    let fd := net_socket(net_af_inet(), net_sock_stream(), net_ipproto_tcp())
    when net_connect(fd, "127.0.0.1", 34567) {
        say "sent:", net_send(fd, "ping")
        say "got:", net_recv(fd, 256)
        net_close(fd)
    }
}
```

`examples/pinger.vy` is a customizable raw-ICMP pinger: per-target count, timeout, payload size, and inter-packet interval, with min/worst/avg and loss reporting, compiled with `-O3 -march=native` and `clock_ns()` timing.

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
