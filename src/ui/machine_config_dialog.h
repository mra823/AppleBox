// AppleBox — machine configuration dialog (86Box-style): CPU-family category
// dropdown → machine model dropdown → per-category tabs.
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <string>

namespace ab {

struct MachineConfig {
    std::string machineId; // selected machine id from the catalog
    int ramSizeKB = 0;     // placeholder; populated per-machine in Phase 1+
};

class MachineConfigDialog {
public:
    explicit MachineConfigDialog(std::filesystem::path romRoot)
        : romRoot_(std::move(romRoot)) {}

    void open() { openRequested_ = true; }

    // Draw the dialog (call every frame). Returns true if the user pressed OK.
    bool draw(MachineConfig& config);

private:
    std::filesystem::path romRoot_;
    bool openRequested_ = false;
    int familyIdx_ = 0;
    int modelIdx_ = 0;
};

} // namespace ab
