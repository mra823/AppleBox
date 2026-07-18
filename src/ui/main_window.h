// AppleBox — main window: SDL2 + OpenGL3 + Dear ImGui shell.
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "core/scheduler.h"
#include "machines/apple1.h"
#include "ui/machine_config_dialog.h"

struct SDL_Window;

namespace ab {

class MainWindow {
public:
    // Returns process exit code. `headlessFrames` > 0 runs N frames without
    // requiring user interaction (used by CI smoke tests).
    int run(int headlessFrames = 0);

private:
    void drawUi();
    void startMachine();
    void drawTerminal();

    Scheduler scheduler_; // idle timeline shown when no machine is running
    MachineConfig machineConfig_;
    MachineConfigDialog configDialog_{std::filesystem::path("roms")};
    bool showDemo_ = false;

    std::unique_ptr<Apple1Machine> apple1_;
    std::string terminal_;
    int terminalCol_ = 0;
    std::string machineError_;
};

} // namespace ab
