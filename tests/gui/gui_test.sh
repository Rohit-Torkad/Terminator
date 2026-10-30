#!/bin/bash
# gui_test.sh - end-to-end GUI test under Xvfb using real X11 key/mouse events
# (xdotool). Assertions are made through files written by the shell under test,
# so no screen scraping is needed. Runs thread mode and process mode.
# Requires: Xvfb, xdotool.  Exits 77 (skip) if they are missing.
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BIN=${TERMINATOR_BIN:-$ROOT/terminator}
command -v Xvfb >/dev/null && command -v xdotool >/dev/null || { echo "SKIP: needs Xvfb and xdotool"; exit 77; }
PASS=0; FAIL=0
DISP=:${GUI_TEST_DISPLAY:-97}
export DISPLAY=$DISP

ok()   { PASS=$((PASS+1)); echo "  ok   [$MODE] $1"; }
bad()  { FAIL=$((FAIL+1)); echo "  FAIL [$MODE] $1  ${2:+($2)}"; }
check() { if [ "$2" == "$3" ]; then ok "$1"; else bad "$1" "expected '$2' got '$3'"; fi; }
T() { xdotool type --delay 20 -- "$1"; }
K() { xdotool key --delay 50 "$@"; }
ENTER() { K Return; }
cmd() { T "$1"; ENTER; sleep "${2:-0.35}"; }
waitfile() { for _ in $(seq 1 40); do [ -e "$1" ] && return 0; sleep 0.1; done; return 1; }
alive() { kill -0 "$APP" 2>/dev/null; }
count_sleep() { ps -eo args= | grep -c -E "^sleep $1\$" || true; }

start_x() {
    Xvfb $DISP -screen 0 1280x800x24 >/dev/null 2>&1 &
    XPID=$!
    for _ in $(seq 1 30); do xdotool getdisplaygeometry >/dev/null 2>&1 && return 0; sleep 0.2; done
    echo "cannot start Xvfb"; exit 1
}
launch() {
    local args=(); [ "$MODE" = process ] && args=(useProcess=True)
    "$BIN" "${args[@]}" --cwd "$W" >"$W/app.log" 2>&1 &
    APP=$!
    for _ in $(seq 1 40); do WID=$(xdotool search --pid "$APP" --name . 2>/dev/null | head -1); [ -n "$WID" ] && break; sleep 0.1; done
    [ -n "$WID" ] || { bad "window appears" "no window"; return 1; }
    xdotool windowfocus "$WID" 2>/dev/null; sleep 0.4
}
stop_app() { kill -TERM "$APP" 2>/dev/null; for _ in $(seq 1 40); do alive || return 0; sleep 0.1; done; kill -KILL "$APP" 2>/dev/null; }

scenario() {
    W=$(mktemp -d /tmp/termgui.XXXXXX); export HOME=$W/home TERMINATOR_HISTFILE=$W/hist TERMINATOR_RC=$W/rc
    mkdir -p "$HOME"; cd "$W" || exit 1
    echo "unique content" > uniquefilename_abc.txt
    launch || { stop_app; return; }
    alive && ok "GUI launches and window is mapped"

    cmd "echo hello > $W/r1"; check "typing + Enter runs a command" "hello" "$(cat $W/r1 2>/dev/null)"
    cmd "nosuchcmd_zzz"; alive && ok "command-not-found does not crash the GUI"

    # --- tabs, independent working directories, shared history ---
    cmd "echo shared-marker-xyz > /dev/null"
    K ctrl+shift+t; sleep 0.6
    cmd "cd /tmp"; cmd "pwd > $W/p2"
    K alt+1; sleep 0.3
    cmd "pwd > $W/p1"
    check "tab 1 cwd unchanged by tab 2's cd" "$W" "$(cat $W/p1)"
    check "tab 2 has its own cwd" "/tmp" "$(cat $W/p2)"
    K alt+2; sleep 0.3
    cmd "history > $W/h2"; grep -q shared-marker-xyz "$W/h2" && ok "history shared between tabs" || bad "history shared between tabs"
    K Up; K Up; K Up; sleep 0.2; K ctrl+u
    K alt+1; sleep 0.2

    # --- line editing shortcuts ---
    T "ho abc > $W/e1"; K ctrl+a; T "ec"; ENTER; sleep 0.4;  check "Ctrl+A moves to line start" "abc" "$(cat $W/e1 2>/dev/null)"
    T "echo ac > $W/e2"; K ctrl+e; ENTER; sleep 0.3;          check "Ctrl+E moves to line end" "ac" "$(cat $W/e2 2>/dev/null)"
    T "echo ac > $W/e2b"; for _ in $(seq 1 $((${#W}+5+3))); do K Left; done; T "b"; ENTER; sleep 0.4
    check "Left moves cursor / insert mid-line" "abc" "$(cat $W/e2b 2>/dev/null)"
    T "echo r > $W/e2c"; K ctrl+a; for _ in 1 2 3 4 5; do K Right; done; T "x"; ENTER; sleep 0.4
    check "Right moves cursor" "xr" "$(cat $W/e2c 2>/dev/null)"
    T "garbage"; K ctrl+u; T "echo u > $W/e3"; ENTER; sleep 0.4;  check "Ctrl+U deletes to line start" "u" "$(cat $W/e3 2>/dev/null)"
    T "echo k > $W/e4 JUNK"; for _ in 1 2 3 4 5; do K Left; done; K ctrl+k; ENTER; sleep 0.4; check "Ctrl+K deletes to line end" "k" "$(cat $W/e4 2>/dev/null)"
    T "echo w > $W/e5 foo"; K ctrl+w; ENTER; sleep 0.4;        check "Ctrl+W deletes previous word" "w" "$(cat $W/e5 2>/dev/null)"
    cmd "echo up >> $W/e6"; K Up; ENTER; sleep 0.4;            check "Up arrow recalls previous command" "2" "$(wc -l < $W/e6 | tr -d ' ')"
    K Up; K Up; K Down; sleep 0.2; K ctrl+u
    K ctrl+r; T "echo up >> $W/e6"; ENTER; sleep 0.3; ENTER; sleep 0.4
    check "Ctrl+R finds the exact match" "3" "$(wc -l < $W/e6 | tr -d ' ')"
    K ctrl+r; T "up >> $W/e"; ENTER; sleep 0.3; ENTER; sleep 0.4
    check "Ctrl+R falls back to longest common substring" "4" "$(wc -l < $W/e6 | tr -d ' ')"
    K ctrl+r; T "qqqqqqzz"; ENTER; sleep 0.3; T "echo nomatch-ok > $W/e7"; ENTER; sleep 0.4
    check "Ctrl+R with no match restores the line" "nomatch-ok" "$(cat $W/e7 2>/dev/null)"
    T "cat uniquefilen"; K Tab; sleep 0.2; T "> $W/c1"; ENTER; sleep 0.4
    check "Tab completes a filename" "unique content" "$(cat $W/c1 2>/dev/null)"
    touch "$W/ambig_one" "$W/ambig_two"
    T "cat ambig_"; K Tab; sleep 0.3; T "2"; ENTER; sleep 0.2; T "> $W/c2"; ENTER; sleep 0.4
    check "ambiguous Tab -> numbered list -> select" "ok" "$([ -e $W/c2 ] && echo ok)"
    T "echo scrollback"; K ctrl+l; T " > $W/e8"; ENTER; sleep 0.4; check "Ctrl+L keeps the input line" "scrollback" "$(cat $W/e8 2>/dev/null)"
    # clipboard: mouse selection -> Ctrl+Shift+C -> Ctrl+Shift+V (our own selection), and paste from another X client
    K ctrl+l; cmd "echo SELTEXT" 0.4
    xdotool mousemove 8 58 mousedown 1 mousemove 120 58 mousemove 300 58 mouseup 1; sleep 0.3
    K ctrl+shift+c; sleep 0.2; T "echo "; K ctrl+shift+v; sleep 0.3; T " > $W/cp1"; ENTER; sleep 0.4
    check "mouse-select + Ctrl+Shift+C / Ctrl+Shift+V" "SELTEXT" "$(cat $W/cp1 2>/dev/null)"
    if command -v xclip >/dev/null; then
        printf 'echo external-clip > %s/cp2' "$W" | xclip -selection clipboard -i >/dev/null 2>&1 &
        sleep 0.5; K ctrl+shift+v; sleep 0.5; ENTER; sleep 0.4
        check "Ctrl+Shift+V pastes text owned by another X client" "external-clip" "$(cat $W/cp2 2>/dev/null)"
        pkill -x xclip 2>/dev/null
    fi

    # --- signals / job control ---
    cmd "sleep 100" 0.5; K ctrl+c; sleep 0.5; cmd "echo alive > $W/a1"; check "Ctrl+C interrupts job; prompt returns" "alive" "$(cat $W/a1 2>/dev/null)"
    check "Ctrl+C killed the sleep" "0" "$(count_sleep 100)"
    cmd "sleep 101" 0.5; K ctrl+z; sleep 0.5; cmd "jobs > $W/j1"; grep -q Stopped "$W/j1" && ok "Ctrl+Z stops the job" || bad "Ctrl+Z stops the job"
    cmd "bg %1"; cmd "jobs > $W/j2"; grep -q Running "$W/j2" && ok "bg resumes the stopped job" || bad "bg resumes the stopped job"
    cmd "fg %1" 0.5; K ctrl+c; sleep 0.5; check "fg + Ctrl+C" "0" "$(count_sleep 101)"
    cmd "multiWatch [\"echo mw >> $W/mw\"]" 2.6; K ctrl+c; sleep 0.6
    check "multiWatch reruns (>=2 runs in 2.6 s)" "ok" "$([ "$(wc -l < $W/mw)" -ge 2 ] && echo ok)"
    n=$(wc -l < $W/mw); sleep 2.5; check "Ctrl+C stops multiWatch" "$n" "$(wc -l < $W/mw)"
    cmd "echo after-mw > $W/a2"; check "prompt works after multiWatch" "after-mw" "$(cat $W/a2 2>/dev/null)"

    # --- GUI stays responsive while a command runs ---
    cmd "sleep 4" 0.2
    start=$(date +%s.%N); K ctrl+shift+t; sleep 0.5; cmd "echo second-tab > $W/r2" 0.4
    xdotool windowsize "$WID" 700 450; sleep 0.3; xdotool windowsize "$WID" 1000 600; sleep 0.3
    el=$(echo "$(date +%s.%N) - $start" | bc)
    [ -e $W/r2 ] && (( $(echo "$el < 3.5" | bc) )) && ok "GUI + other tab respond while 'sleep 4' runs in tab 1" || bad "GUI responsive during long command" "elapsed $el"
    xdotool mousemove 20 14 click 1; sleep 0.3      # click on tab 1 in the tab bar
    alive && ok "mouse click on a tab / resize do not crash" || bad "mouse/resize crash"
    K ctrl+c; sleep 0.5

    # --- closing / crashing a tab ---
    K alt+2; sleep 0.3; cmd "echo \$\$ > $W/tabpid"; TABPID=$(cat $W/tabpid)
    if [ "$MODE" = process ]; then
        check "process mode: tab shell is a separate process" "yes" "$([ "$TABPID" != "$APP" ] && echo yes)"
        kill -SEGV "$TABPID"; sleep 1.2
        alive && ok "crashing one tab process does not take down the GUI" || bad "GUI survived tab crash"
        K alt+1; sleep 0.3; cmd "echo tab1-ok > $W/a3"; check "other tab still works after a crash" "tab1-ok" "$(cat $W/a3 2>/dev/null)"
    else
        check "thread mode: tabs share the GUI process" "$APP" "$TABPID"
        cmd "exit"; sleep 0.6
        alive && ok "'exit' closes just that tab" || bad "'exit' closes just that tab"
    fi
    # --- clean shutdown, no orphans ---
    K alt+1; sleep 0.2; cmd "sleep 120 &" 0.3
    stop_app; sleep 0.5
    check "clean shutdown: GUI gone" "gone" "$(alive && echo alive || echo gone)"
    check "clean shutdown: background job killed (no orphans)" "0" "$(count_sleep 120)"
    check "clean shutdown: no stray terminator processes" "0" "$(pgrep -c -x terminator || true)"

    # --- persistent custom key bindings across restarts ---
    printf 'bind line_start = Ctrl+B\nbind kill_word = Alt+BackSpace\nbind bogus_action = Ctrl+Q\n' > "$TERMINATOR_RC"
    launch
    T "ho bound > $W/b1"; K ctrl+b; T "ec"; ENTER; sleep 0.4
    check "custom binding (Ctrl+B = line_start) active after restart" "bound" "$(cat $W/b1 2>/dev/null)"
    alive && ok "invalid line in ~/.terminatorrc is ignored safely (GUI still running)"
    cmd "bind line_end Ctrl+N > /dev/null"; check "bind command writes the rc file" "1" "$(grep -c 'line_end = Ctrl+N' $TERMINATOR_RC)"
    T "ho ne > $W/b3"; K ctrl+b; T "ec"; K ctrl+n; T "_"; ENTER; sleep 0.4
    check "new binding (Ctrl+N = line_end) takes effect without restart" "ok" "$([ -e $W/b3_ ] && echo ok)"
    stop_app
    cd /; rm -rf "$W"
}

start_x
for MODE in thread process; do scenario; done
kill $XPID 2>/dev/null
echo "gui_test.sh: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
