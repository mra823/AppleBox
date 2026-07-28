// AppleBox — 16K language card softswitch tests (Phase 2d).
// Exercises the $C080-$C08F decode directly through the bus: bank select,
// read source, and the two-consecutive-reads rule that enables writing.
// Needs no ROM image, so it always runs.
// SPDX-License-Identifier: MIT
#include <array>
#include <cstdio>

#include "machines/apple2plus.h"

namespace {

int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

constexpr ab::u8 kRomFill = 0x5a; // distinguishes ROM reads from card RAM

// Touch a language-card switch the way the CPU would.
void sw(ab::Apple2PlusMachine& m, ab::u16 addr) { m.read8(addr); }

} // namespace

int main() {
    ab::Apple2PlusMachine m;
    std::array<ab::u8, 0x3000> rom{};
    rom.fill(kRomFill);
    m.setRom(rom);
    m.reset();

    // --- Reset state: ROM is readable so the reset vector can be fetched ---
    CHECK(m.hasLanguageCard());
    CHECK(!m.lcReadRam());
    CHECK(m.read8(0xd000) == kRomFill);
    CHECK(m.read8(0xffff) == kRomFill);
    std::printf("PASS reset maps ROM into $D000-$FFFF\n");

    // --- $C080: read RAM, writes disabled --------------------------------
    sw(m, 0xc080);
    CHECK(m.lcReadRam());
    CHECK(!m.lcWriteRam());
    CHECK(!m.lcBank1());
    m.write8(0xd000, 0x11);
    CHECK(m.read8(0xd000) == 0x00); // write was ignored, card RAM still clear
    std::printf("PASS $C080 reads card RAM with writes disabled\n");

    // --- A single odd-address read must not enable writing ----------------
    sw(m, 0xc081);
    CHECK(!m.lcWriteRam());
    CHECK(!m.lcReadRam()); // $C081 reads ROM
    CHECK(m.read8(0xd000) == kRomFill);
    std::printf("PASS one read of $C081 does not enable writing\n");

    // --- Two consecutive reads do enable it -------------------------------
    sw(m, 0xc081);
    CHECK(m.lcWriteRam());
    m.write8(0xd000, 0x22); // lands in bank 2
    m.write8(0xe000, 0x33); // lands in the shared $E000-$FFFF space
    sw(m, 0xc080);          // read RAM, bank 2
    CHECK(m.read8(0xd000) == 0x22);
    CHECK(m.read8(0xe000) == 0x33);
    std::printf("PASS two reads of $C081 enable writing to card RAM\n");

    // --- An even-address access disarms and disables ----------------------
    sw(m, 0xc081);
    sw(m, 0xc082); // even: clears both the pre-write flag and write enable
    CHECK(!m.lcWriteRam());
    sw(m, 0xc081); // only one read since the reset, so still disabled
    CHECK(!m.lcWriteRam());
    std::printf("PASS an even-address access disarms the write flip-flop\n");

    // --- A write access to an odd address must not arm --------------------
    sw(m, 0xc082); // clear state
    m.write8(0xc081, 0x00);
    m.write8(0xc081, 0x00);
    CHECK(!m.lcWriteRam());
    std::printf("PASS writes to $C081 never enable card RAM writes\n");

    // --- Bank 1 vs bank 2 share $E000 but not $D000 -----------------------
    sw(m, 0xc08b); // bank 1, read+write RAM (needs the double read to write)
    sw(m, 0xc08b);
    CHECK(m.lcBank1());
    CHECK(m.lcReadRam());
    CHECK(m.lcWriteRam());
    m.write8(0xd000, 0x44);
    m.write8(0xe000, 0x55);
    CHECK(m.read8(0xd000) == 0x44);

    sw(m, 0xc083); // bank 2, read+write RAM
    sw(m, 0xc083);
    CHECK(!m.lcBank1());
    CHECK(m.read8(0xd000) == 0x22); // bank 2 kept its own value
    CHECK(m.read8(0xe000) == 0x55); // $E000 is shared, so bank 1's write shows
    std::printf("PASS $D000 banks are separate and $E000-$FFFF is shared\n");

    // --- Card contents survive reset; the mapping does not ----------------
    m.reset();
    CHECK(!m.lcReadRam());
    CHECK(m.read8(0xd000) == kRomFill);
    sw(m, 0xc080);
    CHECK(m.read8(0xd000) == 0x22);
    std::printf("PASS reset remaps ROM but preserves card contents\n");

    // --- With no card fitted, $D000-$FFFF is always ROM -------------------
    ab::Apple2PlusMachine bare;
    bare.setRom(rom);
    bare.setLanguageCard(false);
    bare.reset();
    sw(bare, 0xc083);
    sw(bare, 0xc083);
    CHECK(bare.read8(0xd000) == kRomFill);
    bare.write8(0xd000, 0x66);
    CHECK(bare.read8(0xd000) == kRomFill);
    std::printf("PASS a 48K II+ ignores the language-card switches\n");

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all language card tests passed\n");
    return 0;
}
