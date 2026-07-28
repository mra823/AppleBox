// AppleBox — Apple II+ machine: 6502 @ ~1.023 MHz, 48K RAM, Autostart ROM,
// softswitch I/O, keyboard latch, speaker toggle, text/graphics soft flags.
// Phase 2a: text-mode machine core; NTSC video, Disk II and slots follow.
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <deque>
#include <filesystem>
#include <functional>
#include <span>
#include <string>

#include "core/bus.h"
#include "core/scheduler.h"
#include "cpu/m6502/m6502_core.h"
#include "devices/disk2.h"

namespace ab {

class Apple2PlusMachine final : public BusInterface {
public:
    static constexpr u32 kClockHz = 1'020'484; // NTSC average (with stretch)
    // Keystrokes are presented at most one per this interval (~5 ms).
    static constexpr Ticks kKeyFeedCycles = 5000;

    Apple2PlusMachine();

    // ROMs: 12 KB Autostart+Applesoft image at $D000-$FFFF.
    bool loadRoms(const std::filesystem::path& romRoot);
    void setRom(std::span<const u8> rom); // 12288 bytes

    // Disk II card in slot 6. Present only when its 256-byte boot ROM has
    // been supplied; without it the machine still boots to BASIC.
    Disk2Controller& disk2() { return disk2_; }
    const Disk2Controller& disk2() const { return disk2_; }

    // 16K language card in slot 0 (making a 64K II+). Software that needs
    // 64K — Integer BASIC from disk, ProDOS, many later titles — requires it.
    void setLanguageCard(bool present) { hasLanguageCard_ = present; }
    bool hasLanguageCard() const { return hasLanguageCard_; }
    // Bank 1 vs bank 2 of the $D000-$DFFF window, and whether reads/writes
    // in $D000-$FFFF land on card RAM rather than ROM.
    bool lcBank1() const { return lcBank1_; }
    bool lcReadRam() const { return lcReadRam_; }
    bool lcWriteRam() const { return lcWriteRam_; }

    void reset();
    void run(Ticks cycles);

    // Keyboard: queue ASCII; presented via the $C000 latch / $C010 strobe.
    void typeChar(char c);
    void typeString(const std::string& s);

    // Video soft flags (Phase 2a: text page rendering only).
    bool textMode() const { return text_; }
    bool mixedMode() const { return mixed_; }
    bool page2() const { return page2_; }
    bool hires() const { return hires_; }

    // Decode the active text page: 24 rows of 40 ASCII characters.
    // Inverse/flash characters are decoded to their base glyph.
    std::array<std::string, 24> textScreen() const;

    Scheduler& scheduler() { return scheduler_; }
    M6502Core& cpu() { return cpu_; }
    const std::array<u8, 0xC000>& ram() const { return ram_; }

    // Speaker toggles since power-on ($C030 accesses); audio lands in 2b+.
    u64 speakerToggles() const { return speakerToggles_; }

    // BusInterface
    u8 read8(u32 addr, AddrSpace sp = AddrSpace::Flat) override;
    void write8(u32 addr, u8 val, AddrSpace sp = AddrSpace::Flat) override;

private:
    u8 ioAccess(u16 addr, bool isWrite, u8 val);
    void languageCardSwitch(u16 addr, bool isWrite);
    // Offset into lcRam_ for a $D000-$FFFF address, honouring the bank select.
    std::size_t lcOffset(u16 addr) const {
        if (addr < 0xe000)
            return (lcBank1_ ? 0u : 0x1000u) + (addr - 0xd000u);
        return 0x2000u + (addr - 0xe000u);
    }
    void feedKeyboard();

    Scheduler scheduler_;
    M6502Core cpu_;

    std::array<u8, 0xC000> ram_{};  // $0000-$BFFF
    std::array<u8, 0x3000> rom_{};  // $D000-$FFFF
    bool hasRom_ = false;
    Disk2Controller disk2_;         // slot 6

    // Language card: bank 1 and bank 2 of $D000-$DFFF then $E000-$FFFF.
    std::array<u8, 0x4000> lcRam_{};
    bool hasLanguageCard_ = true;
    bool lcBank1_ = false;   // reset selects bank 2
    bool lcReadRam_ = false; // reset reads ROM, so the machine can boot
    bool lcWriteRam_ = true;
    bool lcPreWrite_ = false; // odd-address read seen; a second one enables writes

    // Keyboard latch: bit 7 = strobe, bits 6..0 = last key.
    u8 kbdLatch_ = 0;
    std::deque<u8> keyQueue_;
    Scheduler::EventId keyFeedEvent_ = 0;

    // Video softswitches (reset state: text page 1, lores).
    bool text_ = true, mixed_ = false, page2_ = false, hires_ = false;
    std::array<bool, 4> annunciators_{};

    u64 speakerToggles_ = 0;
};

} // namespace ab
