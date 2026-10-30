#!/bin/bash
# demo.sh - prints the demonstration plan, then optionally starts Terminator.
cat <<'PLAN'
Terminator demo plan (about 10 minutes). Type these in the window.

 1. Basics        pwd ; ls -la ; echo "Hello, Terminator" ; gcc examples/hello.c -o hello && ./hello
 2. Tabs + cwd    Ctrl+Shift+T (new tab inherits cwd) ; cd /tmp in tab 2 ; Alt+1 / Alt+2 ; click the tabs and "+"
 3. Quoting       echo 'single $HOME' "double $HOME" ; echo "line one
                  (press Enter: the ">" continuation prompt appears) line two" ; echo $(echo outer $(echo inner))
 4. Pipelines     ls *.c | wc -l ; cat examples/names.txt | sort | uniq -c | head -3
 5. Redirection   sort < examples/names.txt > /tmp/sorted.txt ; cat < /nonexistent   (error, command not run)
 6. Jobs/signals  sleep 100 (Ctrl+C) ; sleep 100 (Ctrl+Z) ; jobs ; bg %1 ; fg %1 (Ctrl+C) ; sleep 50 & ; jobs
 7. multiWatch    multiWatch ["date +%T", "ls | wc -l"]  (watch both update every 2 s, then Ctrl+C)
 8. History       history ; Up/Down ; Ctrl+R then "echo" ; open a 2nd tab and press Up (history is shared)
 9. Completion    cat exam<Tab> ; ls examples/<Tab><Tab> (numbered list: type a number + Enter)
10. Editing       Ctrl+A/E/U/K/W, Left/Right, Ctrl+L ; select text with the mouse, Ctrl+Shift+C / Ctrl+Shift+V
11. Scripts       ./examples/conditionals.sh 7 ; bash examples/check.sh examples ; source examples/basic_commands.sh
12. Key bindings  bind                       (list) ;  bind line_start Ctrl+B   (persisted in ~/.terminatorrc)
13. Process mode  restart with: ./terminator useProcess=True ; echo $$ in two tabs (different PIDs) ;
                  from another shell: kill -SEGV <tab pid>   -> only that tab dies, the rest keeps working
PLAN
read -r -p "Start Terminator in thread mode now? [y/N] " a
[ "$a" = y ] && exec "$(dirname "$0")/../terminator" --cwd "$(dirname "$0")/.."
