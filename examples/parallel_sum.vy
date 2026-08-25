# OpenMP-backed multicore loop with an explicit, race-free reduction.
start {
    let count := 100_000_000
    var total: i64 = 0
    let before := clock_ns()

    parallel each i in 0 ..= count reduce(+ total) {
        total += i
    }

    let elapsed := clock_ns() - before
    say "sum:", total
    say "elapsed ns:", elapsed
    say "workers:", cpu_count()
    check total == count * (count + 1) / 2, "parallel reduction failed"
}
