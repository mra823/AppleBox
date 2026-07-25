// AppleBox — Disk II controller card (Apple 5.25" drive interface).
// Models the "Woz machine": four stepper phase magnets positioning the head
// in quarter-track steps, a motor, and a shift register clocked at one bit
// cell (4 us) that frames nibbles by their high bit.
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "core/types.h"
#include "media/disk_image.h"

namespace ab {

class StateVisitor;

class Disk2Controller {
public:
    static constexpr int kDrives = 2;
    // 1 bit cell = 4 us. At the Apple II's ~1.0205 MHz that is 4.082 CPU
    // cycles; the fractional part is carried so long-term speed stays right.
    static constexpr double kCyclesPerBit = 4.0;

    struct Drive {
        std::optional<media::DiskImage> disk;
        int quarterTrack = 0;   // head position, 0..159
        u32 bitPos = 0;         // rotational position within the track
        u8 phaseMagnets = 0;    // bitmask of energised stepper phases
        std::string imagePath;
    };

    // `cycles` is the machine's CPU cycle counter (used for bit timing).
    u8 io(u8 offset, bool isWrite, u8 value, s64 cycles);

    // Boot ROM at $Cs00 (256 bytes, user-supplied).
    void setBootRom(std::span<const u8> rom);
    bool hasBootRom() const { return hasBootRom_; }
    u8 readRom(u8 offset) const { return bootRom_[offset]; }

    bool insertDisk(int drive, const std::filesystem::path& path,
                    std::string* error = nullptr);
    void ejectDisk(int drive);

    const Drive& drive(int i) const { return drives_[i]; }
    int selectedDrive() const { return selected_; }
    bool motorOn() const { return motorOn_; }
    // Current head position in whole tracks (for the UI).
    double headTrack() const { return drives_[selected_].quarterTrack / 4.0; }

    void reset();
    void serialize(StateVisitor& v);

private:
    void spinTo(s64 cycles);
    void updateStepper(int drive, int phase, bool on);

    std::array<Drive, kDrives> drives_{};
    int selected_ = 0;
    bool motorOn_ = false;
    bool writeMode_ = false; // Q7
    bool loadMode_ = false;  // Q6
    u8 dataRegister_ = 0;
    int holdCells_ = 0; // bit cells a completed nibble has been held
    s64 lastCycles_ = 0;
    double bitFraction_ = 0.0;

    std::array<u8, 256> bootRom_{};
    bool hasBootRom_ = false;
};

} // namespace ab
