// AppleBox — Disk II controller card.
// SPDX-License-Identifier: MIT
#include "devices/disk2.h"

#include <algorithm>

#include "core/savestate.h"

namespace ab {

void Disk2Controller::setBootRom(std::span<const u8> rom) {
    std::copy_n(rom.begin(), std::min<std::size_t>(rom.size(), 256),
                bootRom_.begin());
    hasBootRom_ = true;
}

void Disk2Controller::removeCard() {
    bootRom_.fill(0);
    hasBootRom_ = false;
    reset();
}

bool Disk2Controller::insertDisk(int drive, const std::filesystem::path& path,
                                 std::string* error) {
    if (drive < 0 || drive >= kDrives) return false;
    auto img = media::DiskImage::load(path, error);
    if (!img) return false;
    drives_[drive].disk = std::move(img);
    drives_[drive].bitPos = 0;
    drives_[drive].imagePath = path.string();
    return true;
}

void Disk2Controller::ejectDisk(int drive) {
    if (drive < 0 || drive >= kDrives) return;
    drives_[drive].disk.reset();
    drives_[drive].imagePath.clear();
}

void Disk2Controller::reset() {
    selected_ = 0;
    motorOn_ = false;
    motorOffCycle_ = -kSpinDownCycles;
    writeMode_ = false;
    loadMode_ = false;
    dataRegister_ = 0;
    shifter_ = 0;
    bitFraction_ = 0.0;
    for (auto& d : drives_) d.phaseMagnets = 0;
}

// The head is positioned in half-track steps: the energised phase magnet
// attracts the nearest half-track whose index has that phase's remainder.
void Disk2Controller::updateStepper(int drive, int phase, bool on) {
    Drive& d = drives_[drive];
    const u8 mask = static_cast<u8>(1 << phase);
    if (on)
        d.phaseMagnets |= mask;
    else
        d.phaseMagnets &= static_cast<u8>(~mask);
    if (!on) return;

    int halfTrack = d.quarterTrack / 2; // 0..79
    const int diff = (phase - (halfTrack & 3)) & 3;
    if (diff == 1)
        ++halfTrack; // magnet is one step outward
    else if (diff == 3)
        --halfTrack; // one step inward
    // diff 0 (already aligned) and 2 (ambiguous pull) leave the head put.
    halfTrack = std::clamp(halfTrack, 0, 79);

    const int newQuarter = halfTrack * 2;
    if (newQuarter != d.quarterTrack) {
        // Rotational position is continuous across a seek; scale it into the
        // new track's length.
        d.quarterTrack = newQuarter;
        if (d.disk) {
            const auto& t = d.disk->track(d.quarterTrack);
            d.bitPos = t.bitCount ? d.bitPos % t.bitCount : 0;
        }
    }
}

void Disk2Controller::spinTo(s64 cycles) {
    const s64 elapsed = cycles - lastCycles_;
    lastCycles_ = cycles;
    if (!spinning(cycles) || elapsed <= 0) return;

    bitFraction_ += static_cast<double>(elapsed) / kCyclesPerBit;
    s64 cells = static_cast<s64>(bitFraction_);
    bitFraction_ -= static_cast<double>(cells);
    // A long gap (the CPU ignoring the drive) needs no bit-by-bit catch-up
    // beyond a couple of revolutions.
    cells = std::min<s64>(cells, 120000);

    Drive& d = drives_[selected_];
    if (!d.disk) return;
    const media::Track& track = d.disk->track(d.quarterTrack);
    if (track.empty()) {
        // Unformatted surface (e.g. a half-track): nothing assembles.
        dataRegister_ = 0;
        return;
    }

    for (s64 i = 0; i < cells; ++i) {
        if (writeMode_) {
            // Writing is not modelled yet; the head still advances so
            // rotational position stays honest.
            d.bitPos = (d.bitPos + 1) % track.bitCount;
            continue;
        }
        // Free-running read: bits shift in continuously while the motor
        // turns, exactly as the drive does. Since every disk nibble has its
        // high bit set, the register self-frames on nibble boundaries; a
        // completed nibble is latched for the CPU and assembly continues.
        // The disk does NOT wait for the CPU: software that samples the data
        // register to decide whether the drive is spinning (DOS's RWTS does,
        // to skip its one-second motor spin-up wait) sees it keep changing.
        const u8 bit = track.bit(d.bitPos) ? 1 : 0;
        d.bitPos = (d.bitPos + 1) % track.bitCount;
        shifter_ = static_cast<u8>((shifter_ << 1) | bit);
        if (shifter_ & 0x80) {
            dataRegister_ = shifter_;
            shifter_ = 0;
        }
    }
}

u8 Disk2Controller::io(u8 offset, bool isWrite, u8 value, s64 cycles) {
    spinTo(cycles);

    switch (offset & 0x0f) {
        case 0x0: case 0x1: case 0x2: case 0x3:
        case 0x4: case 0x5: case 0x6: case 0x7:
            updateStepper(selected_, (offset >> 1) & 3, offset & 1);
            break;
        case 0x8:
            if (motorOn_) motorOffCycle_ = cycles; // start the coast
            motorOn_ = false;
            break;
        case 0x9: motorOn_ = true; break;
        case 0xa: selected_ = 0; break;
        case 0xb: selected_ = 1; break;
        case 0xc: loadMode_ = false; break; // Q6L
        case 0xd: loadMode_ = true; break;  // Q6H
        case 0xe: writeMode_ = false; break; // Q7L
        case 0xf: writeMode_ = true; break;  // Q7H
    }

    if (!writeMode_) {
        if (!loadMode_) {
            // Read the data register; the latched nibble is consumed so the
            // CPU's next poll waits for a fresh one.
            const u8 v = dataRegister_;
            if (!isWrite && (v & 0x80)) dataRegister_ = 0;
            return v;
        }
        // Q6H + Q7L: write-protect sense in bit 7.
        const Drive& d = drives_[selected_];
        return (d.disk && d.disk->writeProtected()) ? 0x80 : 0x00;
    }
    (void)value;
    return 0x00;
}

void Disk2Controller::serialize(StateVisitor& v) {
    v.value("disk2.selected", selected_);
    v.value("disk2.motorOn", motorOn_);
    v.value("disk2.motorOffCycle", motorOffCycle_);
    v.value("disk2.writeMode", writeMode_);
    v.value("disk2.loadMode", loadMode_);
    v.value("disk2.dataRegister", dataRegister_);
    v.value("disk2.shifter", shifter_);
    v.value("disk2.lastCycles", lastCycles_);
    v.value("disk2.bitFraction", bitFraction_);
    for (int i = 0; i < kDrives; ++i) {
        const std::string p = "disk2.drive" + std::to_string(i);
        v.value(p + ".quarterTrack", drives_[i].quarterTrack);
        v.value(p + ".bitPos", drives_[i].bitPos);
        v.value(p + ".phaseMagnets", drives_[i].phaseMagnets);
    }
}

} // namespace ab
