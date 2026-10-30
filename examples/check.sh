#!/bin/bash
# check.sh - usage: ./check.sh <directory> : reports whether it exists and how many files it has
if [ ! -d "$1" ]; then
  echo "error: '$1' is not a directory"
  exit 2
fi
COUNT=$(ls "$1" | wc -l)
echo "$1 contains $COUNT entries"
if [ "$COUNT" -gt 5 ]; then echo "(that is quite a lot)"; fi
exit 0
