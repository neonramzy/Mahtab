#!/bin/sh
# Build with w64devkit / MinGW-w64. Produces Mahtab.exe (tray app, no console window).
set -e
NAME=Mahtab   # output file name (Mahtab.exe); change together with kAppName / kAppId in src/appinfo.h

# Compile the icon resource (only if app.rc exists and app.res is missing or older)
if [ -f app.rc ]; then
    windres app.rc -O coff -o app.res
    RES=app.res
else
    RES=
fi

g++ -O2 -std=c++17 -Wall -Wextra -static -s -mwindows \
    -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
    -o "$NAME.exe" \
    src/app.cpp src/engine.cpp src/config.cpp src/core.cpp src/win_gamma.cpp src/win_overlay.cpp \
    $RES \
    -lcomctl32 -ladvapi32 -lshell32 -lgdi32 -luser32
echo "Built $NAME.exe"
