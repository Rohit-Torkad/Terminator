#!/bin/bash
# basic_commands.sh - variables, command substitution, quoting, redirection
GREETING="Hello, Terminator"
echo "$GREETING"
echo 'single quotes keep $GREETING literal'
echo "today's directory has $(ls | wc -l) entries"
echo "nested: $(echo outer $(echo inner))"
echo "script got $# arguments; first is '$1'"
echo written-by-script > /tmp/terminator_example.txt
cat /tmp/terminator_example.txt
