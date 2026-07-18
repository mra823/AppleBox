// AppleBox — Apple I machine tests.
// Part 1 (always runs): PIA + terminal wiring exercised by a hand-assembled
// echo ROM (original code, MIT — no Apple ROM required).
// Part 2 (skips if roms/apple1/wozmon.rom is absent): boots the user-supplied
// Woz Monitor to the "\" prompt, enters a hex program via the keyboard and
// runs it — the Phase 1 acceptance test.
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "machines/apple1.h"

using ab::u8;

namespace {

// Polling echo loop using the PIA exactly as the Woz Monitor does:
//   FF00  LDY #$7F / STY $D012      (DDRB: PB7 input, PB6..0 output)
//         LDA #$A7 / STA $D011 / STA $D013
//   FF0D  loop: LDA $D011 / BPL loop
//         LDA $D010                  (read key, clears CA1 flag)
//   FF15  echo: BIT $D012 / BMI echo (wait for display ready)
//         STA $D012 / JMP loop
constexpr u8 kEchoRom[] = {
    0xa0, 0x7f,             // FF00 LDY #$7F
    0x8c, 0x12, 0xd0,       // FF02 STY $D012
    0xa9, 0xa7,             // FF05 LDA #$A7
    0x8d, 0x11, 0xd0,       // FF07 STA $D011
    0x8d, 0x13, 0xd0,       // FF0A STA $D013
    0xad, 0x11, 0xd0,       // FF0D LDA $D011
    0x10, 0xfb,             // FF10 BPL $FF0D
    0xad, 0x10, 0xd0,       // FF12 LDA $D010
    0x2c, 0x12, 0xd0,       // FF15 BIT $D012
    0x30, 0xfb,             // FF18 BMI $FF15
    0x8d, 0x12, 0xd0,       // FF1A STA $D012
    0x4c, 0x0d, 0xff,       // FF1D JMP $FF0D
};

int testEchoRom() {
    ab::Apple1Machine m;
    std::array<u8, 0x100> rom{};
    std::copy(std::begin(kEchoRom), std::end(kEchoRom), rom.begin());
    rom[0xfc] = 0x00; // reset vector $FF00
    rom[0xfd] = 0xff;
    m.setWozMonitor(rom);

    std::string out;
    m.onDisplayChar = [&](char c) { out.push_back(c); };

    m.reset();
    m.typeString("AB\r");
    m.run(2'000'000);

    if (out != "AB\r") {
        std::printf("FAIL echo: got \"%s\" (%zu chars), want \"AB\\r\"\n",
                    out.c_str(), out.size());
        return 1;
    }
    std::printf("PASS echo ROM (PIA + terminal wiring)\n");
    return 0;
}

int testWozMonitor(const std::filesystem::path& romRoot) {
    ab::Apple1Machine m;
    if (!m.loadRoms(romRoot)) {
        std::printf("SKIP: roms/apple1/wozmon.rom not present (user-supplied)\n");
        return 77;
    }

    std::string out;
    m.onDisplayChar = [&](char c) { out.push_back(c); };

    m.reset();
    m.run(1'000'000);
    if (out.find('\\') == std::string::npos) {
        std::printf("FAIL wozmon: no \"\\\" prompt after reset (got \"%s\")\n",
                    out.c_str());
        return 1;
    }

    // Enter LDA #$D1 / JSR $FFEF (ECHO) / JMP $FF1A (GETLINE) at $0000,
    // then run it: expect 'Q' ($D1 & $7F) — a char never typed.
    m.typeString("0:A9 D1 20 EF FF 4C 1A FF\r");
    m.run(8'000'000);
    std::size_t mark = out.size();
    m.typeString("0R\r");
    m.run(8'000'000);

    if (out.find('Q', mark) == std::string::npos) {
        std::printf("FAIL wozmon: program output missing 'Q'; tail: \"%s\"\n",
                    out.substr(mark).c_str());
        return 1;
    }
    std::printf("PASS Woz Monitor boot + hand-entered program\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path romRoot = "roms";
    if (argc > 1) romRoot = argv[1];
    else if (const char* env = std::getenv("APPLEBOX_ROM_DIR")) romRoot = env;

    int rc1 = testEchoRom();
    if (rc1 != 0) return rc1;
    int rc2 = testWozMonitor(romRoot);
    return rc2 == 77 ? 0 : rc2; // echo test passed; wozmon skip is soft
}
