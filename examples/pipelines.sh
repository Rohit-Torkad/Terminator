#!/bin/bash
# pipelines.sh - pipelines combined with redirection and wildcards
cat names.txt | sort | uniq -c | sort -rn | head -3
sort < names.txt | uniq > /tmp/terminator_unique.txt
wc -l < /tmp/terminator_unique.txt
ls *.sh | wc -l
