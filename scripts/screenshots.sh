#!/bin/bash
# screenshots.sh - regenerate docs/screenshots/*.png under Xvfb (needs Xvfb, xdotool, imagemagick).
ROOT=$(cd "$(dirname "$0")/.." && pwd); OUT=$ROOT/docs/screenshots; mkdir -p "$OUT"
export DISPLAY=:96
Xvfb $DISPLAY -screen 0 1024x640x24 >/dev/null 2>&1 & XPID=$!; sleep 1.5
W=$(mktemp -d); export HOME=$W/home TERMINATOR_HISTFILE=$W/hist TERMINATOR_RC=$W/rc; mkdir -p "$HOME/project/docs"
cp "$ROOT"/examples/* "$HOME/project/"; touch "$HOME/project/docs/alpha.txt" "$HOME/project/docs/alpine.txt"
T() { xdotool type --delay 25 -- "$1"; }; K() { xdotool key --delay 60 "$@"; }
shoot() { sleep 0.4; import -window root -crop "${3:-620x330}+0+0" +repage "$OUT/$1.png"; }
run() {   # $1=name suffix, $2=args
    "$ROOT/terminator" $2 --cwd "$HOME/project" >/dev/null 2>&1 & APP=$!; sleep 1.5
    xdotool windowfocus "$(xdotool search --pid $APP --name . | head -1)" 2>/dev/null
    T 'echo "Hello, Terminator"'; K Return; sleep .3
    T 'ls | head -3'; K Return; sleep .3
    T 'cat missing.txt'; K Return; sleep .3
    K ctrl+shift+t; sleep .6; T 'cd docs'; K Return; sleep .3
    T 'ls al'; K Tab; sleep .3; shoot "01-tabs-completion$1"
    K Return; K ctrl+u; K alt+1; sleep .2
    T 'sleep 100'; K Return; sleep .4; K ctrl+z; sleep .4; T 'jobs'; K Return; sleep .3
    T 'multiWatch ["date +%T", "ls | wc -l"]'; K Return; sleep 3.2; shoot "02-jobs-multiwatch$1" 620x440
    K ctrl+c; sleep .4; T 'echo $$ ; history | tail -3'; K Return; sleep .4; shoot "03-after-ctrl-c$1"
    kill -TERM $APP; sleep 1
}
run "" ""
run "-process-mode" "useProcess=True"
kill $XPID; rm -rf "$W"; ls "$OUT"
