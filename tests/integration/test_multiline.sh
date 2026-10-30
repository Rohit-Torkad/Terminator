#!/bin/bash
. "$(dirname "$0")/lib.sh"
t() {
    expect "double quote spanning lines" $'a\nb' "$(printf 'echo "a\nb"\n' | run)"
    expect "single quote spanning lines" $'x\ny' "$(printf "echo 'x\ny'\n" | run)"
    expect "backslash continuation" "abcdef" "$(printf 'echo abc\\\ndef\n' | run)"
    expect "continuation inside words" "hello world" "$(printf 'echo hello \\\nworld\n' | run)"
    expect "unfinished substitution continues" "ab" "$(printf 'echo $(echo a\n; echo b)\n' | sed 's/;/ /' | run | tr -d '\n ')"
    expect "multi-line nested substitution" "inner" "$(printf 'echo $(echo $(echo inner\n))\n' | run)"
    expect "trailing pipe continues" "HI" "$(printf 'echo hi |\ntr a-z A-Z\n' | run)"
    expect "operators in quotes survive across lines" "a;b|c" "$(printf 'echo "a;b|\nc"\n' | run | tr -d '\n' | sed 's/|/|/')" 
    out=$(printf 'echo "open\nmore"\necho done\n' | TERMINATOR_HEADLESS_PROMPTS=1 run)
    expect "continuation prompt '>' shown for incomplete input" $'$ > open\nmore\n$ done\n$ ' "$(echo "$out" | sed 's/^\$ > open/$ > open/')" 
    expect "incomplete command is not executed early" "" "$(printf 'touch early_marker \\\n' | run >/dev/null; ls early_marker 2>/dev/null)"
    expect "syntax error reported, shell lives" "alive" "$(printf '| bad\necho alive\n' | run 2>/dev/null)"
    expect_match "syntax error message" "syntax error" "$(printf 'cat >\n' | run >/dev/null; errout)"
    expect "Ctrl+C abandons a partial command" "fresh" "$(printf 'echo "never\n@@INT@@\n@@WAIT 500@@\necho fresh\n' | run)"
    expect "comments ignored" "visible" "$(printf '# nothing\necho visible # trailing\n' | run)"
}
for_modes t
summary
