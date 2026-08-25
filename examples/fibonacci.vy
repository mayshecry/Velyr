task fibonacci(n: i64) -> i64 {
    when n < 2 {
        give n
    } otherwise {
        give fibonacci(n - 1) + fibonacci(n - 2)
    }
}

start {
    let n := 42
    let before := clock_ns()
    let answer := fibonacci(n)
    let elapsed := clock_ns() - before

    say "fib", n, "=", answer
    say "elapsed ns:", elapsed
    check answer == 267914296, "fibonacci result was wrong"
}
