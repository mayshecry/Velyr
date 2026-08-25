#!/bin/sh
set -eu
cd "$(dirname "$0")/.."

work="${TMPDIR:-/tmp}/velyr-tests-$$"
mkdir -p "$work"
trap 'rm -rf "$work"' EXIT INT TERM

bin/velyr check tests/basic.vy >/dev/null
bin/velyr build tests/basic.vy -o "$work/basic" --portable >/dev/null
actual="$($work/basic)"
expected='50 positive true
45'
[ "$actual" = "$expected" ] || {
    printf 'basic output mismatch\nexpected:\n%s\nactual:\n%s\n' "$expected" "$actual" >&2
    exit 1
}

bin/velyr build tests/parallel.vy -o "$work/parallel" --portable >/dev/null
[ "$($work/parallel)" = 'parallel ok' ]

bin/velyr build examples/calculator.vy -o "$work/calculator" --portable >/dev/null
calc_add="$(printf '+\n12.5\n7.25\n' | "$work/calculator")"
printf '%s' "$calc_add" | grep -q 'result: 19.75' || {
    printf 'calculator add mismatch\noutput:\n%s\n' "$calc_add" >&2
    exit 1
}
calc_invalid="$(printf '*\nnot_a_number\n42\n2\n' | "$work/calculator")"
printf '%s' "$calc_invalid" | grep -q 'please enter a number' || {
    printf 'calculator retry missing\noutput:\n%s\n' "$calc_invalid" >&2
    exit 1
}
printf '%s' "$calc_invalid" | grep -q 'result: 84' || {
    printf 'calculator retry result mismatch\noutput:\n%s\n' "$calc_invalid" >&2
    exit 1
}
calc_divzero="$(printf '/\n1\n0\n' | "$work/calculator")"
printf '%s' "$calc_divzero" | grep -q 'division by zero' || {
    printf 'calculator div-by-zero not detected\noutput:\n%s\n' "$calc_divzero" >&2
    exit 1
}
calc_eof="$(printf '' | "$work/calculator")"
printf '%s' "$calc_eof" | grep -q 'unsupported operator' || {
    printf 'calculator did not terminate cleanly on EOF\noutput:\n%s\n' "$calc_eof" >&2
    exit 1
}

if bin/velyr check tests/type_error.vy >"$work/error.out" 2>&1; then
    echo 'type-error test unexpectedly passed' >&2
    exit 1
fi
grep -q "immutable" "$work/error.out"

bin/velyr build examples/hello.vy -o "$work/hello" --emit-c "$work/hello.generated.c" --portable >/dev/null
test -s "$work/hello.generated.c"
grep -q 'int main' "$work/hello.generated.c"

echo 'all Velyr tests passed'
