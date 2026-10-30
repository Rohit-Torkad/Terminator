#!/bin/bash
. "$(dirname "$0")/lib.sh"
t() {
    printf 'a.txt\n' > a.txt; touch b.txt c.txt; printf 'pear\napple\nfig\napple\n' > names.txt
    expect "two-stage pipeline" "4" "$(echo 'ls *.txt | wc -l' | run | tr -d ' ')"
    expect "three-stage pipeline" $'apple\napple\nfig' "$(echo 'cat names.txt | sort | head -3' | run)"
    expect "five-stage pipeline" "2" "$(echo 'cat names.txt | sort | uniq | head -2 | wc -l' | run | tr -d ' ')"
    expect "pipeline + input + output redirection" $'      2 apple\n      1 fig\n      1 pear' \
        "$(printf 'sort < names.txt | uniq -c > counts.txt\ncat counts.txt\n' | run)"
    expect "xargs rm" "" "$(printf 'ls *.txt | xargs rm\nls\n' | run | grep -E '\\.txt$' | grep -v -E 'names|counts')"
    expect "quoted operators are not pipes" "a | b" "$(echo 'echo "a | b"' | run)"
    expect "substitution containing a pipe" "HI" "$(echo 'echo $(echo hi | tr a-z A-Z)' | run)"
    expect "builtin in pipeline" "HELLO" "$(echo 'echo hello | tr a-z A-Z' | run)"
    expect "history in pipeline" "1" "$( ( export TERMINATOR_HISTFILE=$WORK/h; printf 'history | wc -l\n' | run | tr -d ' ' ) )"
    expect "large data, no deadlock" "100000" "$(echo 'seq 1 100000 | sort -n | tail -1' | run)"
    expect "reader gets EOF when writer exits" "5" "$(echo 'seq 1 5 | cat | wc -l' | run | tr -d ' ')"
    expect "writer stopped by reader (SIGPIPE)" "y" "$(echo 'yes | head -1' | run)"
    expect "status of pipeline = last command" $'1\n0' "$(printf 'true | false\necho $?\nfalse | true\necho $?\n' | run)"
    expect "stderr of every stage reaches tab" "2" "$(printf 'cat /nope1 | cat /nope2\n' | run; errout | grep -c 'No such file')"
    expect "&& and ||" $'ok\nfallback' "$(printf 'true && echo ok\nfalse && echo no\nfalse || echo fallback\n' | run)"
    expect "semicolon list" $'1\n2' "$(echo 'echo 1; echo 2' | run)"
    expect "no leaked fds (60 pipelines)" "60" "$(for i in $(seq 1 60); do echo 'echo x | cat | cat'; done | run | wc -l | tr -d ' ')"
    expect "children are reaped (no zombies)" "0" "$(printf 'echo a | cat | cat\ntrue\nps -o stat= --ppid $$ | grep -c Z\n' | run | tail -1)"
}
for_modes t
summary
