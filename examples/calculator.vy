# A simple interactive calculator built on Velyr's stdin helpers:
#   read_line()    -- read one line from stdin as a str
#   is_number(s)   -- is the whole string a valid number?
#   parse_number(s) -- parse a str into an f64 (0.0 if it is not a number)
#   eof()          -- true once stdin has reached end-of-file

# Reads a number from the terminal, re-prompting until the user enters
# something parseable. `print` writes without a newline, so the prompt
# stays on the same line as the typed answer.
task read_number(prompt: str) -> f64 {
    var good := false
    var value := 0.0
    while not good and not eof() {
        print prompt
        let line := read_line()
        when is_number(line) {
            value = parse_number(line)
            good = true
        } otherwise {
            say "  (please enter a number such as 42 or 3.14)"
        }
    }
    give value
}

start {
    say "Velyr calculator"

    print "operator (+, -, *, /): "
    let op := read_line()
    let a := read_number("a: ")
    let b := read_number("b: ")

    when op == "+" {
        say "result:", a + b
    } otherwise when op == "-" {
        say "result:", a - b
    } otherwise when op == "*" {
        say "result:", a * b
    } otherwise when op == "/" and b != 0.0 {
        say "result:", a / b
    } otherwise {
        say "division by zero, or unsupported operator", op
    }
}
