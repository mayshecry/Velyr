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

## Comments

Both forms are accepted:

```velyr
# shell-style comment
// C-style line comment
```
