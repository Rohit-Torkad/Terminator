#!/bin/bash
. "$(dirname "$0")/lib.sh"
t() {
    printf 'one\ntwo\n' > in.txt
    expect "output redirection creates file" "hello" "$(printf 'echo hello > out.txt\ncat out.txt\n' | run)"
    expect "> truncates" "second" "$(printf 'echo first > out.txt\necho second > out.txt\ncat out.txt\n' | run)"
    expect ">> appends" $'first\nsecond' "$(printf 'echo first > o.txt\necho second >> o.txt\ncat o.txt\n' | run)"
    expect "input redirection" $'one\ntwo' "$(echo 'cat < in.txt' | run)"
    expect "input + output together" $'ONE\nTWO' "$(printf 'tr a-z A-Z < in.txt > up.txt\ncat up.txt\n' | run)"
    expect "redirect with substitution target" "x" "$(printf 'echo x > $(echo sub.txt)\ncat sub.txt\n' | run)"
    expect "redirect to quoted name with spaces" "sp" "$(printf 'echo sp > "my file.txt"\ncat "my file.txt"\n' | run)"
    expect "builtin output redirection" "$PWD" "$(printf 'pwd > pwd.txt\ncat pwd.txt\n' | run)"
    run <<< 'cat < missing.txt' >/dev/null
    expect_match "missing input file reported" "missing.txt: No such file" "$(errout)"
    expect "command not run when input redirect fails" "absent" "$(printf 'touch should_not_exist < missing.txt\n' | run 2>/dev/null; [ -e should_not_exist ] && echo PRESENT || echo absent)"
    expect "status 1 after failed redirect" "1" "$(printf 'cat < missing.txt\necho $?\n' | run 2>/dev/null)"
    expect_match "unwritable output reported" "(No such file|Permission denied|Not a directory)" "$(printf 'echo x > /proc/nonexistent/f\n' | run >/dev/null; errout)"
    expect "shell survives failed redirect" "alive" "$(printf 'cat < missing.txt\necho alive\n' | run 2>/dev/null)"
    expect "two jobs reading the same file do not interfere" $'one\ntwo\none\ntwo' \
        "$(printf 'cat < in.txt > r1.txt &\ncat < in.txt > r2.txt &\n@@WAIT 500@@\ncat r1.txt r2.txt\n' | run | grep -v '^\[')"
    expect "redirect after pipe stage" "2" "$(printf 'cat in.txt | wc -l > n.txt\ncat n.txt\n' | run | tr -d ' ')"
    expect "tabs' cwd used for relative redirect" "$PWD/s/f.txt" "$(mkdir -p s; printf 'cd s\necho x > f.txt\n' | run; ls $PWD/s/f.txt)"
}
for_modes t
summary
