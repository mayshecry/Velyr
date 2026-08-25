start {
    let n := 1_000_000
    var sum: i64 = 0
    parallel each i in 1 ..= n reduce(+ sum) {
        sum += i
    }
    check sum == n * (n + 1) / 2
    say "parallel ok"
}
