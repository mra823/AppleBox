// AppleBox — DOS 3.3 boot acceptance test (Phase 2c).
// Boots the DOS 3.3 System Master through the real Disk II boot ROM and RWTS,
// then runs CATALOG, which re-seeks and reads the VTOC and directory sectors.
// Together these exercise the whole media path: image loading and sector skew,
// GCR 6-and-2 nibbles, the stepper, and the read shift register.
//
// The disk image is user-supplied and never committed. Point the test at one
// with APPLEBOX_DOS33_DISK, or drop it at disks/. Skips (77) when the ROMs or
// the image are absent.
// SPDX-License-Identifier: MIT
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "machines/apple2plus.h"

namespace {

namespace fs = std::filesystem;

bool screenContains(ab::Apple2PlusMachine& m, const std::string& needle) {
    for (const auto& line : m.textScreen())
        if (line.find(needle) != std::string::npos) return true;
    return false;
}

void dumpScreen(ab::Apple2PlusMachine& m, const char* label) {
    std::printf("--- %s (motor=%d track=%.1f) ---\n", label,
                static_cast<int>(m.disk2().motorOn()), m.disk2().headTrack());
    for (const auto& line : m.textScreen()) std::printf("|%s|\n", line.c_str());
}

// Returns an empty path when no image is available.
fs::path findDisk() {
    if (const char* env = std::getenv("APPLEBOX_DOS33_DISK")) {
        fs::path p = env;
        return fs::exists(p) ? p : fs::path{};
    }
    const fs::path dir = "disks";
    if (!fs::is_directory(dir)) return {};
    // Any DOS 3.3 master image in disks/ will do; names vary by dump.
    for (const auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        std::string name = e.path().filename().string();
        for (char& c : name) c = static_cast<char>(std::toupper(c));
        const auto ext = e.path().extension().string();
        if ((ext == ".dsk" || ext == ".do" || ext == ".woz") &&
            name.find("DOS") != std::string::npos &&
            name.find("3.3") != std::string::npos)
            return e.path();
    }
    return {};
}

bool expect(ab::Apple2PlusMachine& m, const std::string& needle,
            const char* what) {
    if (screenContains(m, needle)) return true;
    std::printf("FAIL: %s — no \"%s\" on screen\n", what, needle.c_str());
    dumpScreen(m, what);
    return false;
}

} // namespace

int main(int argc, char** argv) {
    fs::path romRoot = "roms";
    if (const char* env = std::getenv("APPLEBOX_ROM_DIR")) romRoot = env;

    ab::Apple2PlusMachine m;
    if (!m.loadRoms(romRoot)) {
        std::printf("SKIP: roms/apple2plus/apple2plus.rom not present\n");
        return 77;
    }
    if (!m.disk2().hasBootRom()) {
        std::printf("SKIP: roms/apple2plus/disk2.rom not present\n");
        return 77;
    }

    const fs::path disk = argc > 1 ? fs::path(argv[1]) : findDisk();
    if (disk.empty()) {
        std::printf("SKIP: no DOS 3.3 disk image "
                    "(set APPLEBOX_DOS33_DISK or populate disks/)\n");
        return 77;
    }

    std::string err;
    if (!m.disk2().insertDisk(0, disk, &err)) {
        std::printf("FAIL: could not insert %s: %s\n", disk.string().c_str(),
                    err.c_str());
        return 1;
    }
    std::printf("disk: %s [%s]\n", disk.filename().string().c_str(),
                m.disk2().drive(0).disk->format().c_str());

    // Boot: the ROM loads track 0, DOS relocates, then HELLO greets.
    m.reset();
    m.run(ab::Apple2PlusMachine::kClockHz * 8);

    if (!expect(m, "DOS VERSION 3.3", "boot")) return 1;
    std::printf("PASS DOS 3.3 boots to the System Master greeting\n");

    // CATALOG re-seeks to the directory track and reads it through RWTS.
    m.typeString("CATALOG\r");
    m.run(ab::Apple2PlusMachine::kClockHz * 5);

    if (!expect(m, "DISK VOLUME", "catalog")) return 1;
    if (!expect(m, "HELLO", "catalog")) return 1;
    if (!expect(m, "APPLESOFT", "catalog")) return 1;
    std::printf("PASS CATALOG reads the directory through RWTS\n");
    return 0;
}
