#!/bin/bash
# lib.sh - helpers shared by the integration tests. Every test runs the *same*
# scenario in thread mode and in process mode; results must be identical.
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BIN=${TERMINATOR_BIN:-$ROOT/terminator}
PASS=0; FAIL=0

setup() {
    WORK=$(mktemp -d /tmp/termtest.XXXXXX)
    export HOME=$WORK/home TERMINATOR_HISTFILE=$WORK/hist TERMINATOR_RC=$WORK/rc
    mkdir -p "$HOME" "$WORK/w"; cd "$WORK/w" || exit 1
}
cleanup() { cd /; rm -rf "$WORK"; }

# run: commands on stdin -> engine stdout; engine stderr goes to $WORK/err
run() {
    local flag=""; [ "$MODE" = process ] && flag="--process"
    timeout "${TEST_TIMEOUT:-30}" "$BIN" --headless $flag --cwd "$PWD" 2>"$WORK/err"
}
errout() { cat "$WORK/err"; }

expect() {  # name expected actual
    if [ "$2" == "$3" ]; then PASS=$((PASS+1)); echo "  ok   [$MODE] $1"
    else FAIL=$((FAIL+1)); echo "  FAIL [$MODE] $1"; echo "    expected: $(printf %q "$2")"; echo "    actual:   $(printf %q "$3")"; fi
}
expect_match() {  # name regex actual
    if [[ "$3" =~ $2 ]]; then PASS=$((PASS+1)); echo "  ok   [$MODE] $1"
    else FAIL=$((FAIL+1)); echo "  FAIL [$MODE] $1"; echo "    pattern: $2"; echo "    actual:  $(printf %q "$3")"; fi
}
# count processes whose command line is exactly `sleep <arg-regex>` (anchored, so it can never match a parent shell)
count_sleep() { ps -eo args= | grep -c -E "^sleep $1\$" || true; }
for_modes() { for MODE in thread process; do setup; "$@"; cleanup; done; }
summary() { echo "$(basename "$0"): $PASS passed, $FAIL failed"; [ "$FAIL" -eq 0 ]; }
