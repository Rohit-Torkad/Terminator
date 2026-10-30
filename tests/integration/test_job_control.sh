#!/bin/bash
. "$(dirname "$0")/lib.sh"
t() {
    expect "Ctrl+C interrupts foreground job (status 130)" "130" "$(printf 'sleep 100\n@@WAIT 300@@\n@@INT@@\necho $?\n' | run)"
    expect "shell keeps working after Ctrl+C" "alive" "$(printf 'sleep 100\n@@WAIT 300@@\n@@INT@@\necho alive\n' | run)"
    expect_match "background job announced" '^\[1\] [0-9]+' "$(printf 'sleep 100 &\n' | run)"
    expect_match "jobs lists running job" '\[1\]\+ +Running +sleep 100 &' "$(printf 'sleep 100 &\njobs\n' | run)"
    out=$(printf 'sleep 100\n@@WAIT 300@@\n@@SUSP@@\njobs\nbg %%1\njobs\nfg %%1\n@@WAIT 300@@\n@@INT@@\necho st=$?\n' | run)
    expect_match "Ctrl+Z stops job" '\[1\]\+ +Stopped +sleep 100' "$out"
    expect_match "bg resumes it" '\[1\]\+ sleep 100 &' "$out"
    expect_match "jobs shows it running again" '\[1\]\+ +Running +sleep 100 &' "$out"
    expect_match "fg brings it back and Ctrl+C kills it" 'st=130' "$out"
    expect "fg on missing job" "1" "$(printf 'fg %%3\necho $?\n' | run 2>/dev/null)"
    expect_match "fg error message" "no such job" "$(printf 'fg %%3\n' | run >/dev/null; errout)"
    expect_match "completed background job reported" 'Done +sleep 0.1' "$(printf 'sleep 0.1 &\n@@WAIT 600@@\njobs\n' | run)"
    expect "background output still reaches the tab" "bgout" "$(printf 'echo bgout &\n@@WAIT 400@@\n' | run | grep -x bgout)"
    # signals reach the whole pipeline (process group), not just one process
    printf 'sleep 101 | sleep 102\n@@WAIT 400@@\n@@INT@@\n' | run >/dev/null; sleep 0.3
    expect "Ctrl+C kills every process of a pipeline" "0" "$(count_sleep '10[12]')"
    # no orphans when the tab goes away
    printf 'sleep 103 &\nsleep 104 &\nexit\n' | run >/dev/null; sleep 0.4
    expect "exit leaves no orphaned children" "0" "$(count_sleep '10[34]')"
    printf 'sleep 105 &\n' | run >/dev/null; sleep 0.4      # stdin EOF => tab closes
    expect "closing the tab kills its background jobs" "0" "$(count_sleep '105')"
    expect "stopped job is resumable with fg and finishes" "done" "$(printf 'sleep 0.6\n@@SUSP@@\n@@WAIT 100@@\nfg\necho done\n' | run | tail -1)"
}
for_modes t
summary
