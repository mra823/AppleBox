// AppleBox — Apple I machine.
// SPDX-License-Identifier: MIT
#include "machines/apple1.h"

#include <algorithm>
#include <cctype>

#include "core/rom_manifest.h"

namespace ab {

Apple1Machine::Apple1Machine() {
    // PB7 is the display-busy handshake (input to the CPU side); PB6..0
    // carry the character. PA7 is tied high on the keyboard connector.
    pia_.portAInput = [this] { return static_cast<u8>(currentKey_ | 0x80); };
    pia_.portBInput = [this] { return static_cast<u8>(displayBusy_ ? 0x80 : 0x00); };
    pia_.portBOutput = [this](u8 val) {
        // Character accepted by the terminal section.
        displayBusy_ = true;
        scheduler_.scheduleIn(displayDoneEvent_, kDisplayBusyCycles);
        cpu_.abortSlice(); // slice bound must honor the new event
        onDisplayChar(static_cast<char>(val & 0x7f));
    };
    // The PIA IRQ outputs are not connected on the Apple I (polled only).

    displayDoneEvent_ = scheduler_.addEvent(
        "apple1.displayDone", [this](Ticks) { displayBusy_ = false; });
    keyFeedEvent_ = scheduler_.addEvent("apple1.keyFeed", [this](Ticks) {
        feedKeyboard();
        if (!keyQueue_.empty())
            scheduler_.scheduleIn(keyFeedEvent_, kKeyFeedCycles);
    });

    cpu_.attachBus(*this);
}

bool Apple1Machine::loadRoms(const std::filesystem::path& romRoot) {
    auto woz = loadRom(romRoot, "apple1", {"wozmon.rom", 0x100, "", false});
    if (!woz) return false;
    setWozMonitor(*woz);
    if (auto basic = loadRom(romRoot, "apple1", {"basic.rom", 0x1000, "", true}))
        setBasicRom(*basic);
    return true;
}

void Apple1Machine::setWozMonitor(std::span<const u8> rom) {
    std::copy_n(rom.begin(), std::min(rom.size(), wozmon_.size()),
                wozmon_.begin());
    hasWozmon_ = true;
}

void Apple1Machine::setBasicRom(std::span<const u8> rom) {
    std::copy_n(rom.begin(), std::min(rom.size(), basic_.size()),
                basic_.begin());
    hasBasic_ = true;
}

void Apple1Machine::reset() {
    pia_.reset();
    displayBusy_ = false;
    keyQueue_.clear();
    currentKey_ = 0;
    cpu_.reset();
}

void Apple1Machine::run(Ticks cycles) {
    const Ticks end = scheduler_.now() + cycles;
    while (scheduler_.now() < end) {
        Ticks target = std::min(end, scheduler_.nextEventTime());
        Ticks consumed = cpu_.run(target - scheduler_.now());
        scheduler_.runUntil(scheduler_.now() + consumed);
    }
}

void Apple1Machine::typeChar(char c) {
    u8 v = static_cast<u8>(std::toupper(static_cast<unsigned char>(c)));
    if (c == '\n' || c == '\r') v = 0x0d;
    keyQueue_.push_back(v);
    if (!scheduler_.isScheduled(keyFeedEvent_))
        scheduler_.scheduleIn(keyFeedEvent_, kKeyFeedCycles);
}

void Apple1Machine::typeString(const std::string& s) {
    for (char c : s) typeChar(c);
}

void Apple1Machine::feedKeyboard() {
    // Present the next key once the previous strobe has been consumed
    // (the monitor's read of KBD clears the CA1 flag).
    if (keyQueue_.empty()) return;
    if (pia_.peek(1) & 0x80) return; // previous key still pending
    currentKey_ = keyQueue_.front();
    keyQueue_.pop_front();
    pia_.setCA1(true); // rising-edge strobe
    pia_.setCA1(false);
}

bool Apple1Machine::idle() const {
    return keyQueue_.empty() && !displayBusy_ && !(pia_.peek(1) & 0x80);
}

u8 Apple1Machine::read8(u32 addr, AddrSpace) {
    addr &= 0xffff;
    if (addr < ram_.size()) return ram_[addr];
    if (addr >= 0xd010 && addr <= 0xd013)
        return pia_.read(static_cast<u8>(addr & 3));
    if (addr >= 0xe000 && addr <= 0xefff && hasBasic_)
        return basic_[addr - 0xe000];
    if (addr >= 0xff00) return wozmon_[addr - 0xff00];
    return 0x00; // open bus (simplified)
}

void Apple1Machine::write8(u32 addr, u8 val, AddrSpace) {
    addr &= 0xffff;
    if (addr < ram_.size()) {
        ram_[addr] = val;
        return;
    }
    if (addr >= 0xd010 && addr <= 0xd013)
        pia_.write(static_cast<u8>(addr & 3), val);
    // ROM writes ignored.
}

} // namespace ab
