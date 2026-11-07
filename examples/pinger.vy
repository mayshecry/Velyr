task sleep_ns(ns: i64) {
    let began := clock_ns()
    while clock_ns() - began < ns {
        relax()
    }
}

task ping_host(host: str, count: i64, timeout_ms: i64, size: i64, interval_ms: i64) -> i64 {
    var received: i64 = 0
    var best: i64 = 0
    var worst: i64 = 0
    var total: i64 = 0
    var first: bool = true
    var sent: i64 = 0
    each i in 0 .. count {
        sent += 1
        let r := net_ping_once(host, timeout_ms, size)
        when r >= 0 {
            received += 1
            when first {
                best = r
                worst = r
                first = false
            } otherwise {
                when r < best { best = r }
                when r > worst { worst = r }
            }
            total += r
            say "  ", i + 1, " ", r, " ns"
        } otherwise {
            say "  ", i + 1, " timeout"
        }
        when interval_ms > 0 {
            sleep_ns(interval_ms * 1000000)
        }
    }
    when sent > 0 {
        say host, " received:", received, "/", sent, " loss:", (sent - received) * 100 / sent, "%"
    } otherwise {
        say host, " received: 0/", sent, " loss: 100%"
    }
    when received > 0 {
        say "  best:", best, " ns worst:", worst, " ns avg:", total / received, " ns"
    }
    give received
}

start {
    var total_sent: i64 = 0
    var total_received: i64 = 0

    let host1 := "1.1.1.1"
    let count1: i64 = 25
    let timeout1: i64 = 1000
    let size1: i64 = 32
    let interval1: i64 = 0

    let host2 := "1.0.0.1"
    let count2: i64 = 25
    let timeout2: i64 = 1000
    let size2: i64 = 64
    let interval2: i64 = 0

    let host3 := "8.8.8.8"
    let count3: i64 = 15
    let timeout3: i64 = 1000
    let size3: i64 = 128
    let interval3: i64 = 0

    let host4 := "8.8.4.4"
    let count4: i64 = 15
    let timeout4: i64 = 1000
    let size4: i64 = 256
    let interval4: i64 = 0

    total_sent += count1
    total_sent += count2
    total_sent += count3
    total_sent += count4

    say "target:", host1
    total_received += ping_host(host1, count1, timeout1, size1, interval1)
    say "target:", host2
    total_received += ping_host(host2, count2, timeout2, size2, interval2)
    say "target:", host3
    total_received += ping_host(host3, count3, timeout3, size3, interval3)
    say "target:", host4
    total_received += ping_host(host4, count4, timeout4, size4, interval4)

    say "total sent:", total_sent, " received:", total_received, " loss:", (total_sent - total_received) * 100 / total_sent, "%"
}
