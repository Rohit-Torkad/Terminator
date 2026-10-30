#!/bin/bash
. "$(dirname "$0")/lib.sh"
t() {
    printf 'line one\nline two\n' > file.txt
    expect "echo with quotes" "Hello, Terminator" "$(echo 'echo "Hello, Terminator"' | run)"
    expect "pwd" "$PWD" "$(echo pwd | run)"
    expect "external ls" "file.txt" "$(echo ls | run)"
    expect "cat file" $'line one\nline two' "$(echo 'cat file.txt' | run)"
    expect "exit status \$?" $'0\n1' "$(printf 'true\necho $?\nfalse\necho $?\n' | run)"
    expect "command not found => 127" "127" "$(printf 'nosuchcmd_xyz\necho $?\n' | run)"
    expect_match "command not found message" "nosuchcmd_xyz: command not found" "$(printf 'nosuchcmd_xyz\n' | run >/dev/null; errout)"
    expect "shell survives errors" "still here" "$(printf 'nosuchcmd_xyz\ncat /no/such/file\necho still here\n' | run)"
    expect "variables" "5 world" "$(printf 'X=5\nY=world\necho $X $Y\n' | run)"
    expect "single quotes literal" 'a $X b' "$(printf "X=1\necho 'a \$X b'\n" | run)"
    expect "double quotes expand" 'a 1 b' "$(printf 'X=1\necho "a $X b"\n' | run)"
    expect "escaped dollar" '$X' "$(printf 'X=1\necho \\$X\n' | run)"
    expect "command substitution" "today is hi" "$(echo 'echo today is $(echo hi)' | run)"
    expect "backtick substitution" "hi" "$(echo 'echo `echo hi`' | run)"
    expect "nested substitution" "deep" "$(echo 'echo $(echo $(echo $(echo deep)))' | run)"
    expect "substitution inside quotes keeps spaces" "a   b" "$(echo 'echo "$(echo "a   b")"' | run)"
    expect "exit status after substitution" "3" "$(printf 'x=$(exit 3)\necho $?\n' | run)"
    # cd / per-tab directory
    mkdir -p sub/deeper
    expect "cd changes shell dir" "$PWD/sub/deeper" "$(printf 'cd sub/deeper\npwd\n' | run)"
    expect "cd - and .." "$PWD/sub
$PWD/sub/deeper
$PWD/sub" "$(printf 'cd sub\npwd\ncd deeper\npwd\ncd ..\npwd\n' | run)"
    expect "cd then relative command" $'deeper\nx.txt' "$(printf 'cd sub\ntouch x.txt\nls\n' | run)"
    expect_match "cd to missing dir fails" "No such file" "$(printf 'cd /nonexistent_dir\n' | run >/dev/null; errout)"
    expect "cd failure status" "1" "$(printf 'cd /nonexistent_dir\necho $?\n' | run)"
    expect "cd to file fails" "1" "$(printf 'cd file.txt\necho $?\n' | run 2>/dev/null)"
    expect "cd ~" "$HOME" "$(printf 'cd ~\npwd\n' | run)"
    # exit
    echo 'exit 7' | run >/dev/null; expect "exit status propagates" "7" "$?"
    # compile and run a C program
    if command -v gcc >/dev/null; then
        printf '#include <stdio.h>\nint main(void){puts("compiled ok");return 0;}\n' > hello.c
        expect "gcc + ./program" "compiled ok" "$(printf 'gcc hello.c -o hello\n./hello\n' | run)"
    fi
    # environment
    expect "export reaches children" "FOO=bar" "$(printf 'export FOO=bar\nenv | grep ^FOO\n' | run)"
    expect "unexported not in env" "" "$(printf 'FOO=bar\nenv | grep ^FOO\n' | run)"
    # history builtin
    expect "history lists commands" $'    1  echo a\n    2  echo b\n    3  history' \
        "$( ( export TERMINATOR_HISTFILE=$WORK/fresh_hist; printf 'echo a\necho b\nhistory\n' | run | grep -v -E '^(a|b)$' ) )"
    expect "wildcard" $'a.txt\nb.txt' "$(touch a.txt b.txt c.md; echo 'echo *.txt' | run | tr ' ' '\n' | grep -E '^(a|b)\.txt$')"
    expect "stdin forwarded to foreground program" "typed text" "$(printf 'cat\n@@WAIT 300@@\n@@LINE typed text\n@@WAIT 200@@\n@@EOF@@\n' | run)"
}
for_modes t
summary
