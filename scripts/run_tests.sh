#!/bin/bash
# run_tests.sh - run every automated test: unit, integration (thread + process mode),
# and the Xvfb GUI test when Xvfb/xdotool are available.
#   SKIP_GUI=1 ./scripts/run_tests.sh      skip the GUI suite
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT" || exit 1
[ -x ./terminator ] || make || exit 1
FAILED=()
echo "=== unit tests"
make -s unit >/tmp/terminator_unit.out 2>&1 && grep -E "passed" /tmp/terminator_unit.out | paste -sd' ' || { cat /tmp/terminator_unit.out; FAILED+=(unit); }
for t in commands pipelines redirection job_control multiline scripts features; do
    echo "=== integration: $t"
    ./tests/integration/test_$t.sh | grep -v '^  ok' || true
    [ "${PIPESTATUS[0]}" -eq 0 ] || FAILED+=("integration/$t")
done
if [ -z "$SKIP_GUI" ]; then
    echo "=== gui (Xvfb)"
    ./tests/gui/gui_test.sh | grep -v '^  ok'; rc=${PIPESTATUS[0]}
    if [ "$rc" -eq 77 ]; then echo "(skipped: Xvfb/xdotool not installed)"; elif [ "$rc" -ne 0 ]; then FAILED+=(gui); fi
fi
echo
if [ ${#FAILED[@]} -eq 0 ]; then echo "ALL TESTS PASSED"; exit 0; fi
echo "FAILED: ${FAILED[*]}"; exit 1
