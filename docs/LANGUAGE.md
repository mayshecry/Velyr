# Velyr language guide (0.1)

Velyr is a small, statically typed, native language. Its surface syntax is intentionally low-noise: words explain control flow, braces show scope, newlines end statements, and punctuation is kept for types and expressions.

## Program shape

A program has zero or more `task` declarations and exactly one `start` block.

```velyr
task square(x: i64) -> i64 {
    give x * x
}

start {
    say square(12)
}
```

Tasks are visible throughout the file, including before their declaration. The supported types are:

| Type | Meaning | Native representation |
|---|---|---|
| `i64` | signed 64-bit integer | `int64_t` |
| `f64` | IEEE-754 double | `double` |
| `bool` | `true` or `false` | C `bool` |
| `str` | immutable byte string | pointer + length |
| `void` | no value; task returns only | C `void` |

## Values

`let` creates an immutable value and `var` creates a mutable value. `:=` asks the compiler to infer the type. An explicit type uses `:`.

```velyr
let width := 1920
let ratio: f64 = 1.7777777778
var frames: i64 = 0
frames += 1
```

Velyr rejects writes to `let` values and incompatible assignments before invoking the native backend. An `i64` can widen to `f64`; narrowing is deliberately not implicit.

Integer separators are allowed: `1_000_000`. Statements end at a newline or `;`.

## Control flow

```velyr
when temperature > 90.0 {
    say "hot"
} otherwise when temperature < 5.0 {
    say "cold"
} otherwise {
    say "normal"
}

while running {
    tick()
}

each i in 0 .. 10 {       # 0 through 9
    say i
}

each i in 0 ..= 10 {      # 0 through 10
    say i
}
```

Use `give value` to return from a task. `break` and `continue` work in sequential loops.

## Safe multicore loops

`parallel each` maps to a statically scheduled native OpenMP loop. An outer mutable value cannot be written unless it is named in an explicit reduction. This catches the most common accidental data race.

```velyr
var total: i64 = 0
parallel each i in 0 .. 100_000_000 reduce(+ total) {
    total += i
}
```

`+` and `*` reductions are supported in 0.1. Loop bodies may read immutable outer values. Output from a parallel loop is synchronized but ordering is intentionally unspecified.

## Output and checks

`say` prints comma-separated values with spaces and appends a newline. `print` does not append a newline.

```velyr
say "answer:", 42
check answer == 42, "answer invariant failed"
```

A failed `check` prints its message, source file, and line, then aborts.

Line input works through `read_line()` (returns a `str` without the trailing newline) and `eof()` (reports end of input). `is_number(s)` and `parse_number(s)` turn user text into an `f64`; see [Input and parsing](#input-and-parsing).

## Input and parsing

`read_line` reads one line from standard input. The trailing newline is removed; at end-of-file it returns an empty `str`. Each returned `str` points into its own slot of a small internal ring buffer, so a few lines can stay alive at once, but reads beyond 16 lines start reusing older slots. `eof()` reports whether stdin has been exhausted.

```velyr
print "name: "
let name := read_line()
when name == "" {
    say "no name given"
}
```

`is_number(s)` tells you whether the whole string is a valid `f64` literal (optional sign, decimal point, exponent). `parse_number(s)` converts it; a string that is not a number converts to `0.0`, so guard with `is_number` when the input must be valid.

```velyr
let line := read_line()
when is_number(line) {
    say "double it:", parse_number(line) * 2.0
} otherwise {
    say "that is not a number"
}
```

A failed read sets the EOF flag; calling `read_line` again after EOF keeps returning `""`.

## Operators

From high to low precedence:

1. unary `-`, `+`, `not`, `!`
2. `*`, `/`, `%`
3. `+`, `-`
4. `<`, `<=`, `>`, `>=`
5. `==`, `!=`
6. `and`, `&&`
7. `or`, `||`

String equality compares contents, not pointers. String concatenation and heap-owned values are intentionally absent from the 0.1 core.

## Hardware built-ins

| Built-in | Result |
|---|---|
| `clock_ns()` | monotonic nanoseconds as `i64` |
| `cycles()` | ordered hardware counter/ticks as `i64` |
| `cpu_count()` | online logical CPU count |
| `relax()` | CPU spin-loop hint |
| `sqrt(number)` | square root as `f64` |

On x86-64, `cycles()` uses fenced `RDTSC` and `relax()` uses `PAUSE`. On AArch64 they use `CNTVCT_EL0` and `YIELD`. Counter frequency is architecture-dependent; use `clock_ns()` for portable elapsed time.

## Networking built-ins

| Built-in | Result |
|---|---|
| `net_af_inet()` | `i64` address family `AF_INET` |
| `net_af_inet6()` | `i64` address family `AF_INET6` |
| `net_sock_stream()` | `i64` socket type `SOCK_STREAM` |
| `net_sock_dgram()` | `i64` socket type `SOCK_DGRAM` |
| `net_sock_raw()` | `i64` socket type `SOCK_RAW` |
| `net_ipproto_icmp()` | `i64` protocol `IPPROTO_ICMP` |
| `net_ipproto_tcp()` | `i64` protocol `IPPROTO_TCP` |
| `net_ipproto_udp()` | `i64` protocol `IPPROTO_UDP` |
| `net_socket(domain, type, protocol)` | `i64` file descriptor |
| `net_connect(fd, addr, port)` | `bool`, TCP connect to `str` address |
| `net_bind(fd, port)` | `bool`, bind to any interface |
| `net_listen(fd, backlog)` | `bool`, listen on a stream socket |
| `net_accept(fd)` | `i64` accepted client fd, `-1` on error |
| `net_send(fd, data)` | `i64` bytes sent, `-1` on error |
| `net_recv(fd, max_len)` | `str` received data |
| `net_sendto(fd, data, addr, port)` | `i64` bytes sent, `-1` on error |
| `net_recvfrom(fd, max_len)` | `str` received datagram |
| `net_close(fd)` | `void`, close the socket |
| `net_setblocking(fd, blocking)` | `void`, toggle `O_NONBLOCK` |
| `net_settimeout(fd, ms)` | `void`, set receive/send timeouts |
| `net_setsockopt(fd, level, optname, value)` | `bool`, `setsockopt` with an `i64` value |
| `net_resolve(host)` | `str` IPv4 address, empty on failure |
| `net_ping_once(host, timeout_ms, size)` | `i64` ICMP round-trip nanoseconds, `-1` on timeout or error |
| `net_ping_fd(fd, addr, port, size)` | `i64` ICMP round-trip nanoseconds on a persistent raw socket, `-1` on timeout or error (no per-call socket creation — lowest overhead) |
| `net_sendmmsg_batch(fd, data, addr, port, count)` | `i64` packets sent via `sendmmsg()`, up to `count` datagrams per syscall |
| `net_sol_socket()` | `i64` `SOL_SOCKET` constant |
| `net_sndbuf()` | `i64` `SO_SNDBUF` constant |

## Command-line arguments

| Built-in | Result |
|---|---|
| `arg_count()` | `i64`, number of user arguments |
| `arg(i)` | `str`, i-th user argument |
| `parse_int(s)` | `i64`, parse string as integer |

`net_ping_once` / `net_ping_fd` use a raw `ICMP` socket and require `CAP_NET_RAW`; without it every call returns `-1`.

See `examples/hyper_ping.vy` for the hardware-close pinger: a single reused raw socket, zero inter-packet delay, `clock_ns()` timing, and min/worst/avg RTT with loss reporting.

## Comments

Both forms are accepted:

```velyr
# shell-style comment
// C-style line comment
```
