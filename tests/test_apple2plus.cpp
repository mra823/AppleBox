// AppleBox — Apple II+ machine tests (Phase 2a).
// Boots the user-supplied Autostart/Applesoft ROM to the "]" prompt, then
// types PRINT 2+2 and verifies the result on the text page. Skips (77) when
// roms/apple2plus/apple2plus.rom is absent.
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

    m.reset();
    m.run(4'000'000); // cold start: banner, slot scan, Applesoft entry

    if (!screenContains(m, "APPLE ][") || !screenContains(m, "]")) {
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
    return 0;
}
