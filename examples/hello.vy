# A Velyr program begins at `start`, so there is no ceremonial main function.
start {
    let language := "Velyr"
    let cores := cpu_count()

    say "Hello from", language
    say "Available CPU threads:", cores
}
