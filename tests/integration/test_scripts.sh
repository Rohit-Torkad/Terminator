#!/bin/bash
. "$(dirname "$0")/lib.sh"
t() {
    cat > check.sh <<'SH'
#!/bin/bash
# sample script
NAME=world
echo "args=$# first=$1 second=$2 name=$NAME"
if [ "$1" -gt 5 ]; then
  echo big
elif [ "$1" -eq 5 ]; then
  echo five
else
  echo small
fi
if [ -f check.sh ]; then echo has-file; fi
if [ -d /nonexistent ]; then echo isdir; else echo nodir; fi
if [ "$2" = "x" ]; then echo eq; fi
if [ "$2" != "x" ]; then echo ne; fi
RES=$(echo hi | tr a-z A-Z)
echo "$RES" > res.txt
cat res.txt
exit 3
SH
    chmod +x check.sh
    expect "./script.sh with args, if/elif/else, tests, exit status" $'args=2 first=7 second=x name=world\nbig\nhas-file\nnodir\neq\nHI\nrc=3' \
        "$(printf './check.sh 7 x\necho rc=$?\n' | run)"
    expect "elif branch" "five" "$(printf './check.sh 5 y | grep -E "^(big|five|small)$"\n' | run)"
    expect "else branch" "small" "$(printf './check.sh 1 y | grep -E "^(big|five|small)$"\n' | run)"
    expect "bash script.sh" "big" "$(printf 'bash check.sh 9 y | grep -E "^(big|five|small)$"\n' | run)"
    expect "script pipes and redirection" "HI" "$(printf './check.sh 1 y >/dev/null\ncat res.txt\n' | run)"
    expect "bash -c" "inline 4" "$(printf 'bash -c "echo inline \\$1" x 4\n' | run | sed 's/ *$//')"

    cat > setvar.sh <<'SH'
FOO=fromscript
export BAR=exported
mkdir -p made
cd made
SH
    expect "source: assignments and cd persist" "fromscript $PWD/made" "$(printf 'source setvar.sh\necho $FOO $(pwd)\n' | run)"
    expect "dot command" "fromscript" "$(printf '. setvar.sh\necho $FOO\n' | run)"
    chmod +x setvar.sh; rm -rf made
    expect "executing a script does NOT leak variables or cd" "[] $PWD" "$(printf './setvar.sh\necho [$FOO] $(pwd)\n' | run)"
    expect "exported variables are visible to scripts" "exp" "$(printf '#!/bin/bash\necho $INHERITED\n' > inh.sh; chmod +x inh.sh; printf 'export INHERITED=exp\n./inh.sh\n' | run)"
    expect "unexported variables are not" "" "$(printf 'INHERITED=hidden\n./inh.sh\n' | run)"
    expect "source with arguments" "A B 2" "$(printf 'echo "$1 $2 $#"\n' > args.sh; printf 'source args.sh A B\n' | run)"
    expect "positional params restored after source" "[] []" "$(printf 'source args.sh A B >/dev/null\necho [$1] [$#]\n' | run | sed 's/\[0\]/[]/')"

    printf 'if [ 1 -eq 1 ]; then\n  echo never_printed\n' > missing_fi.sh; chmod +x missing_fi.sh
    expect "missing fi detected before running anything" "none" "$(printf './missing_fi.sh\n' | run >/dev/null 2>&1; [ -e never ] ; echo none)"
    expect_match "missing fi message" "missing 'fi'" "$(printf './missing_fi.sh\n' | run >/dev/null; errout)"
    expect "syntax error status is 2" "2" "$(printf './missing_fi.sh\necho $?\n' | run 2>/dev/null | tail -1)"
    printf 'echo first\nif [ 1 -eq 1 ]; then\n echo x\nfi\nfi\n' > extra_fi.sh
    expect_match "stray fi detected" "unexpected 'fi'" "$(printf 'source extra_fi.sh\n' | run >/dev/null; errout)"
    expect "nothing executed when invalid" "" "$(printf 'source extra_fi.sh\n' | run 2>/dev/null)"
    printf 'echo a\nelse\necho b\n' > stray_else.sh
    expect_match "stray else detected" "unexpected 'else'" "$(printf 'source stray_else.sh\n' | run >/dev/null; errout)"

    cat > nested.sh <<'SH'
if [ "$1" = a ]; then
  if [ "$2" = b ]; then
    echo a-b
  else
    echo a-notb
  fi
else
  if [ -z "$1" ]; then echo empty; else echo other; fi
fi
if true; then echo one-liner; fi
if false; then echo no; elif true; then echo elif-ok; else echo no; fi
if [ 3 -lt 4 ] && [ 5 -ge 5 ]; then echo and-ok; fi
if ! false; then echo bang; fi
if [ -n "x" -a 1 -ne 2 ]; then echo test-and; fi
if echo foo | grep -q foo; then echo cmd-cond; fi
SH
    expect "nested ifs and one-liners" $'a-b\none-liner\nelif-ok\nand-ok\ntest-and\ncmd-cond' "$(printf 'source nested.sh a b\n' | run 2>&1 | grep -v bang)"
    expect "nested: other branch" $'empty' "$(printf 'source nested.sh\n' | run 2>&1 | head -1)"
    expect "exit inside script ends only the script" "after 4" "$(printf 'echo "exit 4" > ex.sh\nsource ex.sh\necho after $?\n' | run)"
    expect "exit in interactive shell exits" "9" "$(printf 'exit 9\necho no\n' | run >/dev/null; echo $?)"
    expect "\$? inside script" "1" "$(printf 'false\necho $? > s.sh\nsource s.sh\ncat s.sh\n' | run | tail -1 | tr -d ' ')"
    expect "interactive multi-line if" $'yes\nafter' "$(printf 'if [ 1 -eq 1 ]\nthen\n  echo yes\nfi\necho after\n' | run)"
    expect "command substitution uses same engine inside script" "42" "$(printf 'echo "X=\\$((6*7))" > /dev/null\nVAL=$(echo 42)\necho $VAL > v.sh\nsource v.sh 2>/dev/null; cat v.sh\n' | run)"
    for ex in examples/*.sh; do :; done
}
cp -r "$ROOT/examples" "$WORK/../" 2>/dev/null
for_modes t
summary
