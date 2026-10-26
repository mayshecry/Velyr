
start {
    let n := 1_000_000_000
    var sum: i64 = 0
    let cycle_start := cycles()

    parallel each i in 0 .. n reduce(+ sum) {
        sum += i
    }

    let cycle_end := cycles()
    say "sum", sum
    say "hardware ticks", cycle_end - cycle_start
}
