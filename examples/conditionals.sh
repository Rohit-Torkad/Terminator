#!/bin/bash
# conditionals.sh - usage: ./conditionals.sh <number>
if [ "$#" -eq 0 ]; then
  echo "usage: ./conditionals.sh <number>"
  exit 1
fi
if [ "$1" -gt 10 ]; then
  echo "$1 is greater than 10"
elif [ "$1" -eq 10 ]; then
  echo "$1 is exactly 10"
else
  if [ "$1" -lt 0 ]; then echo "$1 is negative"; else echo "$1 is between 0 and 9"; fi
fi
if [ -f names.txt ]; then echo "names.txt exists"; fi
if [ -d /tmp ]; then echo "/tmp is a directory"; fi
if [ "$USER" = "root" ]; then echo "running as root"; else echo "not root"; fi
