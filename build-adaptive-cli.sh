#!/bin/sh
# OPTIONAL. Console prototype of the adaptive (screen-content based) brightness feature.
# Not part of the main app; kept for experiments. Produces Mahtab-cli.exe
set -e
g++ -O2 -std=c++17 -Wall -Wextra -static -s -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
    -o gammy-lite-cli.exe \
    src/cli_main.cpp src/core.cpp src/win_capture.cpp src/win_gamma.cpp \
    -ldxgi -ld3d11 -ldxguid -lgdi32 -luser32
echo "Built Mahtab.exe"
