@echo off
rem Build with w64devkit / MinGW-w64 (g++ must be in PATH). Produces Mahtab.exe
g++ -O2 -std=c++17 -Wall -Wextra -static -s -mwindows -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -o Mahtab.exe src\app.cpp src\engine.cpp src\config.cpp src\core.cpp src\win_gamma.cpp src\win_overlay.cpp -lcomctl32 -ladvapi32 -lshell32 -lgdi32 -luser32
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
echo Built Mahtab.exe
