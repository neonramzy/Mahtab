![Screenshot of Mahtab app](images/mahtab.PNG)

Mahtab

Small, dependency-free screen brightness and color temperature control for Windows (tray app).

by Milad ramzany (GPLv3).

Build: install w64devkit, open its shell in this folder, run `sh build.sh`.

`src/cli_main.cpp` + `src/win_capture.cpp` are an optional console prototype of adaptive
(screen-content based) brightness. They are not part of the app; `sh build-adaptive-cli.sh` builds them.

Tests (any OS with g++):

    g++ -std=c++17 tests/test_core.cpp src/core.cpp -o t1 && ./t1
    g++ -std=c++17 -I tests/stubs -I src tests/engine_test.cpp tests/engine_stubs.cpp \
        src/engine.cpp src/core.cpp -o t2 && ./t2
