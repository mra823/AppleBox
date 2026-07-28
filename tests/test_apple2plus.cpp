// AppleBox — Apple II+ machine tests (Phase 2a).
// With no card in slot 6, boots the user-supplied Autostart/Applesoft ROM to
// the "]" prompt and types PRINT 2+2. With the Disk II card installed and no
// disk in the drive, checks the machine does what real hardware does: spin the
// drive and wait, never reaching BASIC. Skips (77) when the ROM is absent.
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "machines/apple2plus.h"

namespace {

bool screenContains(ab::Apple2PlusMachine& m, const std::string& needle) {
    for (const auto& line : m.textScreen())
        if (line.find(needle) != std::string::npos) return true;
    return false;
}

// The Applesoft prompt starts a line. Searching the whole screen for "]"
// would also match the "APPLE ][" banner, so anchor it to column 0.
bool hasPromptLine(ab::Apple2PlusMachine& m) {
    for (const auto& line : m.textScreen())
        if (!line.empty() && line[0] == ']') return true;
    return false;
}

void dumpScreen(ab::Apple2PlusMachine& m) {
    for (const auto& line : m.textScreen()) std::printf("|%s|\n", line.c_str());
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path romRoot = "roms";
    if (argc > 1) romRoot = argv[1];
    else if (const char* env = std::getenv("APPLEBOX_ROM_DIR")) romRoot = env;

    ab::Apple2PlusMachine m;
    if (!m.loadRoms(romRoot)) {
        std::printf("SKIP: roms/apple2plus/apple2plus.rom not present\n");
        return 77;
    }
    // A II+ with a Disk II card boots the drive, so test the bare machine.
    m.disk2().removeCard();

    m.reset();
    m.run(4'000'000); // cold start: banner, slot scan, Applesoft entry

    if (!screenContains(m, "APPLE ][") || !hasPromptLine(m)) {
        std::printf("FAIL: no Applesoft prompt after cold start\n");
        dumpScreen(m);
        return 1;
    }
    std::printf("PASS Apple II+ cold start to \"]\" prompt\n");

    m.typeString("PRINT 2+2\r");
    m.run(2'000'000);

    if (!screenContains(m, "4")) {
        std::printf("FAIL: PRINT 2+2 produced no '4'\n");
        dumpScreen(m);
        return 1;
    }
    std::printf("PASS PRINT 2+2 -> 4\n");

    // --- Disk II installed, drive empty -----------------------------------
    // Real hardware hangs in the boot ROM with the drive turning; reaching
    // BASIC here would mean the card is not being seen at all.
    ab::Apple2PlusMachine d;
    if (!d.loadRoms(romRoot)) return 1;
    if (d.disk2().hasBootRom()) {
        d.reset();
        d.run(4'000'000);
        if (hasPromptLine(d)) {
            std::printf("FAIL: slot 6 card ignored; fell through to BASIC\n");
            dumpScreen(d);
            return 1;
        }
        if (!d.disk2().motorOn()) {
            std::printf("FAIL: boot ROM did not start the drive motor\n");
            return 1;
        }
        std::printf("PASS Disk II with no disk spins and waits in the boot ROM\n");
    } else {
        std::printf("SKIP disk-II-present case: roms/apple2plus/disk2.rom absent\n");
    }
    return 0;
}
