// AppleBox — Apple I machine: 6502 @ ~1.023 MHz, MC6821 PIA, Woz Monitor.
// PIA map: KBD $D010 (port A), KBDCR $D011, DSP $D012 (port B), DSPCR $D013.
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
#include "devices/pia6821.h"

namespace ab {

class Apple1Machine final : public BusInterface {
public:
    static constexpr u32 kClockHz = 1'022'727; // NTSC 14.31818 MHz / 14
    // Terminal section accepts roughly one character per 60 Hz frame.
    static constexpr Ticks kDisplayBusyCycles = kClockHz / 60;
    // Keystrokes are presented at most one per this interval (~5 ms).
    static constexpr Ticks kKeyFeedCycles = 5000;

    Apple1Machine();

    // ROM loading. Woz Monitor is 256 bytes at $FF00; BASIC 4 KB at $E000.
    // For tests, ROMs may be injected directly.
    bool loadRoms(const std::filesystem::path& romRoot);
    void setWozMonitor(std::span<const u8> rom); // 256 bytes
    void setBasicRom(std::span<const u8> rom);   // 4096 bytes

    void reset();

    // Run ~`cycles` CPU cycles (1 CPU cycle = 1 master tick on the Apple I).
    void run(Ticks cycles);

    // Terminal: queue ASCII keystrokes; display output arrives via callback.
    void typeChar(char c);
    void typeString(const std::string& s);
    std::function<void(char)> onDisplayChar = [](char) {};

    // True when all queued input has been consumed and the display is idle.
    bool idle() const;

    Scheduler& scheduler() { return scheduler_; }
    M6502Core& cpu() { return cpu_; }
    Pia6821& pia() { return pia_; }

    // BusInterface
    u8 read8(u32 addr, AddrSpace sp = AddrSpace::Flat) override;
    void write8(u32 addr, u8 val, AddrSpace sp = AddrSpace::Flat) override;

private:
    void feedKeyboard();

    Scheduler scheduler_;
    M6502Core cpu_;
    Pia6821 pia_;

    std::array<u8, 0x2000> ram_{};   // 8 KB
    std::array<u8, 0x100> wozmon_{}; // $FF00-$FFFF
    std::array<u8, 0x1000> basic_{}; // $E000-$EFFF
    bool hasWozmon_ = false;
    bool hasBasic_ = false;

    std::deque<u8> keyQueue_;
    u8 currentKey_ = 0;
    bool displayBusy_ = false;
    Scheduler::EventId displayDoneEvent_ = 0;
    Scheduler::EventId keyFeedEvent_ = 0;
};

} // namespace ab
