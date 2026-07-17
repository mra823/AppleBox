// AppleBox — entry point.
// SPDX-License-Identifier: MIT
#include <cstring>

#include "ui/main_window.h"

int main(int argc, char** argv) {
    int headlessFrames = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            headlessFrames = std::atoi(argv[++i]);
    }
    ab::MainWindow window;
    return window.run(headlessFrames);
}
