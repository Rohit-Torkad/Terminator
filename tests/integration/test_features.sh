#!/bin/bash
. "$(dirname "$0")/lib.sh"
t() {
    # ---- multiWatch ----
    out=$(printf 'multiWatch ["echo A", "echo B | tr a-z A-Z"]\n@@WAIT 500@@\n@@INT@@\necho back=$?\n' | run)
    expect_match "multiWatch label + Unix timestamp" '"echo A" , current_time: [0-9]{9,}' "$out"
    expect_match "multiWatch separator lines" '<-+>' "$out"
    expect_match "multiWatch runs pipelines" $'\nB\n' "$out"
    expect_match "Ctrl+C returns to the prompt (status 130)" 'back=130' "$out"
    out=$(printf 'multiWatch ["echo A"]\n@@WAIT 2600@@\n@@INT@@\n' | run)
    expect "command is rerun every ~2 s" "2" "$(echo "$out" | grep -c 'current_time')" 
    out=$(printf 'multiWatch ["sleep 1 | cat", "echo fast"]\n@@WAIT 400@@\n@@INT@@\n' | run)
    expect_match "commands run concurrently (fast output while slow one is running)" 'echo fast' "$out"
    printf 'multiWatch ["sleep 107", "sleep 108 | cat"]\n@@WAIT 400@@\n@@INT@@\n' | run >/dev/null; sleep 0.3
    expect "Ctrl+C terminates every watched process" "0" "$(count_sleep '10[78]')"
    expect "terminal usable afterwards" "usable" "$(printf 'multiWatch ["echo x"]\n@@WAIT 300@@\n@@INT@@\necho usable\n' | run | tail -1)"
    expect "multiWatch accepts several quoted commands with commas" "2" "$(printf 'multiWatch ["echo a,b", "echo c"]\n@@WAIT 400@@\n@@INT@@\n' | run | grep -c current_time)"
    printf 'multiWatch [bad]\n' | run >/dev/null; expect_match "multiWatch syntax error" "multiWatch" "$(errout)"
    expect "multiWatch syntax error status" "2" "$(printf 'multiWatch [bad]\necho $?\n' | run 2>/dev/null)"
    expect "a failing command is reported, others continue" "1" "$(printf 'multiWatch ["nosuch_cmd_q", "echo ok"]\n@@WAIT 400@@\n@@INT@@\n' | run 2>/dev/null | grep -c '^ok$')"

    # ---- history ----
    expect "history shows at most 1000 entries" "1000" "$( ( export TERMINATOR_HISTFILE=$WORK/big; for i in $(seq 1 1100); do echo "true $i"; done; echo 'history | wc -l' ) > "$WORK/big_in"; TERMINATOR_HISTFILE=$WORK/big run < "$WORK/big_in" | tr -d ' ')"
    n=$(wc -l < "$WORK/big" | tr -d ' ')
    expect "history numbers are absolute (entry number = lines persisted so far + 1)" "$((n + 1))" "$(echo 'history 1' | TERMINATOR_HISTFILE=$WORK/big run | head -1 | awk '{print $1}')"
    seq 1 10500 | sed 's/^/cmd/' > "$WORK/h10k"
    TERMINATOR_HISTFILE=$WORK/h10k bash -c "echo true | '$BIN' --headless $([ $MODE = process ] && echo --process) --cwd '$PWD'" >/dev/null 2>&1
    expect "history file trimmed to the last 10000 commands" "10001" "$(wc -l < "$WORK/h10k" | tr -d ' ')"
    expect "history file keeps the newest commands" "cmd10500" "$(grep -x cmd10500 "$WORK/h10k")"
    printf 'echo persisted-one\n' | run >/dev/null
    expect "history persists across runs" "echo persisted-one" "$(printf 'history 2\n' | run | head -1 | sed 's/^ *[0-9]*  //')"
    expect "consecutive duplicate commands are stored once" "1" "$( ( export TERMINATOR_HISTFILE=$WORK/dd; printf 'echo d\necho d\necho d\nhistory\n' | run | grep -c 'echo d' ) )"

    # ---- key-binding configuration ----
    expect_match "bind lists defaults" 'line_start +Ctrl\+A' "$(echo bind | run)"
    expect_match "bind persists to the rc file" '^bind line_start = Ctrl\+B$' "$(echo 'bind line_start Ctrl+B' | run >/dev/null; cat "$TERMINATOR_RC")"
    expect_match "bind shows custom" 'line_start +Ctrl\+B +\(custom\)' "$(echo bind | run)"
    echo 'bind line_end Ctrl+B' | run >/dev/null; expect_match "conflicting binding refused" "already bound" "$(errout)"
    echo 'bind nonsense Ctrl+Q' | run >/dev/null; expect_match "unknown action refused" "unknown action" "$(errout)"
    echo 'bind line_end Ctrl+Bogus' | run >/dev/null; expect_match "invalid key refused" "unknown key" "$(errout)"
    echo 'bind line_end x' | run >/dev/null; expect_match "plain printable key refused" "cannot be bound" "$(errout)"
    expect "refused bindings are not written" "1" "$(grep -c '^bind' "$TERMINATOR_RC")"
}
for_modes t
summary
