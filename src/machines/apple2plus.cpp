// AppleBox — Apple II+ machine.
// SPDX-License-Identifier: MIT
#include "machines/apple2plus.h"

#include <algorithm>
#include <cctype>

#include "core/rom_manifest.h"

namespace ab {

Apple2PlusMachine::Apple2PlusMachine() {
    keyFeedEvent_ = scheduler_.addEvent("apple2p.keyFeed", [this](Ticks) {
        feedKeyboard();
        if (!keyQueue_.empty())
            scheduler_.scheduleIn(keyFeedEvent_, kKeyFeedCycles);
    });
    cpu_.attachBus(*this);
}

bool Apple2PlusMachine::loadRoms(const std::filesystem::path& romRoot) {
    auto rom =
        loadRom(romRoot, "apple2plus", {"apple2plus.rom", 0x3000, "", false});
    if (!rom) return false;
    setRom(*rom);
    return true;
}

void Apple2PlusMachine::setRom(std::span<const u8> rom) {
    std::copy_n(rom.begin(), std::min(rom.size(), rom_.size()), rom_.begin());
    hasRom_ = true;
}

void Apple2PlusMachine::reset() {
    kbdLatch_ = 0;
    keyQueue_.clear();
    text_ = true;
    mixed_ = false;
    page2_ = false;
    hires_ = false;
    annunciators_ = {};
    speakerToggles_ = 0;
    cpu_.reset();
}

void Apple2PlusMachine::run(Ticks cycles) {
    const Ticks end = scheduler_.now() + cycles;
    while (scheduler_.now() < end) {
        Ticks target = std::min(end, scheduler_.nextEventTime());
        Ticks consumed = cpu_.run(target - scheduler_.now());
        scheduler_.runUntil(scheduler_.now() + consumed);
    }
}

void Apple2PlusMachine::typeChar(char c) {
    u8 v = static_cast<u8>(std::toupper(static_cast<unsigned char>(c)));
    if (c == '\n' || c == '\r') v = 0x0d;
    keyQueue_.push_back(v);
    if (!scheduler_.isScheduled(keyFeedEvent_))
        scheduler_.scheduleIn(keyFeedEvent_, kKeyFeedCycles);
}

void Apple2PlusMachine::typeString(const std::string& s) {
    for (char c : s) typeChar(c);
}

void Apple2PlusMachine::feedKeyboard() {
    if (keyQueue_.empty()) return;
    if (kbdLatch_ & 0x80) return; // previous key not yet acknowledged
    kbdLatch_ = 0x80 | (keyQueue_.front() & 0x7f);
    keyQueue_.pop_front();
}

std::array<std::string, 24> Apple2PlusMachine::textScreen() const {
    // Text page interleave: row base = page + (r%8)*$80 + (r/8)*$28.
    const u16 page = page2_ ? 0x0800 : 0x0400;
    std::array<std::string, 24> out;
    for (int r = 0; r < 24; ++r) {
        const u16 base = page + (r % 8) * 0x80 + (r / 8) * 0x28;
        std::string line(40, ' ');
        for (int c = 0; c < 40; ++c) {
            u8 sc = ram_[base + c];
            // Screen codes: $00-3F inverse, $40-7F flash, $80-FF normal;
            // low 6 bits select the glyph ($00-1F = @A-Z..., $20-3F = sp!...).
            u8 g = sc & 0x3f;
            line[c] = static_cast<char>(g < 0x20 ? g + 0x40 : g);
        }
        out[r] = std::move(line);
    }
    return out;
}

u8 Apple2PlusMachine::ioAccess(u16 addr, bool isWrite, u8 val) {
    (void)val;
    switch (addr & 0xfff0) {
        case 0xc000: // keyboard latch (reads); writes are no-ops on a II+
            return kbdLatch_;
        case 0xc010: // any access clears the keyboard strobe
            kbdLatch_ &= 0x7f;
            return kbdLatch_;
        case 0xc030: // speaker toggle (any access)
            ++speakerToggles_;
            return 0x00;
        case 0xc050:
            switch (addr & 0x000f) {
                case 0x0: text_ = false; break;
                case 0x1: text_ = true; break;
                case 0x2: mixed_ = false; break;
                case 0x3: mixed_ = true; break;
                case 0x4: page2_ = false; break;
                case 0x5: page2_ = true; break;
                case 0x6: hires_ = false; break;
                case 0x7: hires_ = true; break;
                case 0x8: case 0x9: case 0xa: case 0xb:
                case 0xc: case 0xd: case 0xe: case 0xf:
                    annunciators_[(addr >> 1) & 3] = addr & 1;
                    break;
            }
            return 0x00;
        case 0xc060: // buttons/paddles
            switch (addr & 0x000f) {
                case 0x1: case 0x2: case 0x3: // pushbuttons: not pressed
                    return 0x00;
                case 0x4: case 0x5: case 0x6: case 0x7: // paddle timers
                    return 0x00; // timed out (no paddle)
                default:
                    return 0x00;
            }
        case 0xc070: // paddle trigger
            return 0x00;
        default:
            // Cassette, empty I/O: floating bus (simplified to 0).
            return 0x00;
    }
    (void)isWrite;
}

u8 Apple2PlusMachine::read8(u32 addr, AddrSpace) {
    addr &= 0xffff;
    if (addr < 0xc000) return ram_[addr];
    if (addr < 0xc100) return ioAccess(static_cast<u16>(addr), false, 0);
    if (addr < 0xd000) return 0x00; // empty slot / expansion ROM space
    return hasRom_ ? rom_[addr - 0xd000] : 0x00;
}

void Apple2PlusMachine::write8(u32 addr, u8 val, AddrSpace) {
    addr &= 0xffff;
    if (addr < 0xc000) {
        ram_[addr] = val;
        return;
    }
    if (addr < 0xc100) {
        ioAccess(static_cast<u16>(addr), true, val);
        return;
    }
    // Slot space and ROM: writes ignored.
}

} // namespace ab
