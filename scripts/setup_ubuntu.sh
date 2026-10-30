#!/bin/bash
# setup_ubuntu.sh - install everything needed to build and test Terminator on Ubuntu
# (also works inside an Ubuntu VirtualBox VM on a Mac). Run with: sudo ./scripts/setup_ubuntu.sh
set -e
if [ "$(id -u)" -ne 0 ]; then echo "run as root: sudo $0"; exit 1; fi
apt-get update
# build dependencies (required)
apt-get install -y build-essential pkg-config libx11-dev
# GUI test / screenshot dependencies (optional; the unit and integration tests do not need them)
apt-get install -y xvfb xdotool xclip imagemagick bc x11-utils xfonts-base || \
    echo "note: optional GUI test tools could not be installed; 'make test' will skip the GUI suite"
echo "done. Build with:  make   and run:  ./terminator   (inside a graphical session)"
