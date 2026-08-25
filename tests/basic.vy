task twice(x: i64) -> i64 {
    give x * 2
}

task classify(x: i64) -> str {
    when x < 0 {
        give "negative"
    } otherwise when x == 0 {
        give "zero"
    } otherwise {
        give "positive"
    }
}

start {
    let immutable := 21
    var value := twice(immutable)
    check value == 42
    value += 8
    say value, classify(value), true

    var sum := 0
    each i in 0 .. 10 {
        sum += i
    }
    say sum
    check sum == 45
    check "fast" == "fast"
}
