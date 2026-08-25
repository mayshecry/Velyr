start {
    let n := 1_000_000_000
    var sum: i64 = 0

    let time_start := clock_ns()
    let tick_start := cycles()

    parallel each i in 0 .. n reduce(+ sum) {
        sum += i
    }

    let tick_end := cycles()
    let time_end := clock_ns()

    let elapsed_ns := time_end - time_start
    let elapsed_ticks := tick_end - tick_start

    check sum == n * (n - 1) / 2
    say "sum:", sum

    bench_report(
        "parallel i64 reduction",
        n,
        elapsed_ns,
        elapsed_ticks
    )
}