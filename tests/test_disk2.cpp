// AppleBox — Disk II controller tests (Phase 2c-2).
// Drives the card from a hand-assembled 6502 program that seeks with the
// stepper magnets and reads nibbles with the classic "LDA $C0EC / BPL" loop,
// then checks the recovered sectors against the source image. No Apple ROM
// is involved, so this runs in CI.
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

#include "core/bus.h"
#include "cpu/m6502/m6502_core.h"
#include "devices/disk2.h"
#include "media/gcr6.h"

namespace fs = std::filesystem;
using namespace ab;

namespace {

int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

// DOS 3.3 physical -> logical sector skew (verified against a real WOZ image).
constexpr u8 kDosPhysToLog[16] = {0, 7, 14, 6, 13, 5, 12, 4,
                                  11, 3, 10, 2, 9, 1, 8, 15};

// A 6502 program that (1) starts the motor, (2) takes $02 half-track steps
// by pulsing the stepper phases in sequence, and (3) fills $1000-$2FFF with
// nibbles read through the data register. Assembled by hand; origin $0300.
constexpr u8 kProgram[] = {
    /* 0300 */ 0xad, 0xe9, 0xc0,       // LDA $C0E9   motor on
    /* 0303 */ 0xad, 0xea, 0xc0,       // LDA $C0EA   select drive 1
    /* 0306 */ 0xa9, 0x00,             // LDA #$00
    /* 0308 */ 0x85, 0x03,             // STA $03     phase = 0
    /* 030A */ 0xa5, 0x02,             // seek: LDA $02
    /* 030C */ 0xf0, 0x1a,             // BEQ rdinit ($0328)
    /* 030E */ 0xe6, 0x03,             // INC $03
    /* 0310 */ 0xa5, 0x03,             // LDA $03
    /* 0312 */ 0x29, 0x03,             // AND #$03
    /* 0314 */ 0x85, 0x03,             // STA $03
    /* 0316 */ 0x0a,                   // ASL         A = phase*2
    /* 0317 */ 0xa8,                   // TAY
    /* 0318 */ 0xc8,                   // INY         +1 selects "magnet on"
    /* 0319 */ 0xb9, 0xe0, 0xc0,       // LDA $C0E0,Y
    /* 031C */ 0x20, 0x47, 0x03,       // JSR delay
    /* 031F */ 0x88,                   // DEY
    /* 0320 */ 0xb9, 0xe0, 0xc0,       // LDA $C0E0,Y magnet off
    /* 0323 */ 0xc6, 0x02,             // DEC $02
    /* 0325 */ 0x4c, 0x0a, 0x03,       // JMP seek
    /* 0328 */ 0xa9, 0x00,             // rdinit: LDA #$00
    /* 032A */ 0x85, 0x00,             // STA $00
    /* 032C */ 0xa9, 0x10,             // LDA #$10
    /* 032E */ 0x85, 0x01,             // STA $01     buffer = $1000
    /* 0330 */ 0xa0, 0x00,             // LDY #$00
    /* 0332 */ 0xad, 0xec, 0xc0,       // rdloop: LDA $C0EC
    /* 0335 */ 0x10, 0xfb,             // BPL rdloop
    /* 0337 */ 0x91, 0x00,             // STA ($00),Y
    /* 0339 */ 0xc8,                   // INY
    /* 033A */ 0xd0, 0xf6,             // BNE rdloop
    /* 033C */ 0xe6, 0x01,             // INC $01
    /* 033E */ 0xa5, 0x01,             // LDA $01
    /* 0340 */ 0xc9, 0x30,             // CMP #$30    stop at $3000
    /* 0342 */ 0xd0, 0xee,             // BNE rdloop
    /* 0344 */ 0x4c, 0x44, 0x03,       // done: JMP done
    /* 0347 */ 0xa2, 0x00,             // delay: LDX #$00
    /* 0349 */ 0xca,                   // dl: DEX
    /* 034A */ 0xd0, 0xfd,             // BNE dl
    /* 034C */ 0x60,                   // RTS
};
constexpr u16 kDoneAddr = 0x0344;

// 64K RAM plus the Disk II card at slot 6 ($C0E0-$C0EF).
class TestBus final : public BusInterface {
public:
    std::vector<u8> ram = std::vector<u8>(0x10000, 0);
    Disk2Controller* card = nullptr;
    M6502Core* cpu = nullptr;

    u8 read8(u32 addr, AddrSpace = AddrSpace::Flat) override {
        addr &= 0xffff;
        if (addr >= 0xc0e0 && addr <= 0xc0ef)
            return card->io(static_cast<u8>(addr & 0x0f), false, 0,
                            cpu->cycles());
        return ram[addr];
    }
    void write8(u32 addr, u8 val, AddrSpace = AddrSpace::Flat) override {
        addr &= 0xffff;
        if (addr >= 0xc0e0 && addr <= 0xc0ef) {
            card->io(static_cast<u8>(addr & 0x0f), true, val, cpu->cycles());
            return;
        }
        ram[addr] = val;
    }
};

std::vector<u8> makeSectorImage() {
    std::vector<u8> img(143360);
    std::mt19937 rng(999);
    for (int t = 0; t < 35; ++t)
        for (int s = 0; s < 16; ++s) {
            u8* sec = img.data() + (t * 16 + s) * 256;
            sec[0] = static_cast<u8>(t);
            sec[1] = static_cast<u8>(s);
            for (int i = 2; i < 256; ++i) sec[i] = static_cast<u8>(rng());
        }
    return img;
}

struct FoundSector {
    u8 track = 0, sector = 0;
    std::array<u8, 256> data{};
};

// Scans a captured nibble stream for address/data field pairs.
std::vector<FoundSector> scanNibbles(std::span<const u8> nibs) {
    std::vector<FoundSector> out;
    FoundSector pending;
    bool havePending = false;
    for (std::size_t i = 0; i + 2 < nibs.size(); ++i) {
        if (nibs[i] != 0xd5 || nibs[i + 1] != 0xaa) continue;
        if (nibs[i + 2] == 0x96 && i + 11 < nibs.size()) {
            const u8* f = nibs.data() + i + 3;
            const u8 vol = media::decode44(f[0], f[1]);
            pending.track = media::decode44(f[2], f[3]);
            pending.sector = media::decode44(f[4], f[5]);
            const u8 sum = media::decode44(f[6], f[7]);
            havePending = sum == (vol ^ pending.track ^ pending.sector);
            i += 10;
        } else if (nibs[i + 2] == 0xad && havePending &&
                   i + 3 + media::kSectorNibbles62 <= nibs.size()) {
            if (media::decodeSector62(nibs.subspan(i + 3,
                                                  media::kSectorNibbles62),
                                      pending.data))
                out.push_back(pending);
            havePending = false;
            i += 2 + media::kSectorNibbles62;
        }
    }
    return out;
}

// Runs the program with `halfSteps` stepper steps; returns captured nibbles.
std::vector<u8> runCapture(const fs::path& diskPath, u8 halfSteps,
                           Disk2Controller& card) {
    M6502Core cpu;
    TestBus bus;
    bus.card = &card;
    bus.cpu = &cpu;
    cpu.attachBus(bus);

    std::string err;
    if (!card.insertDisk(0, diskPath, &err)) {
        std::printf("FAIL insertDisk: %s\n", err.c_str());
        ++failures;
        return {};
    }
    card.reset();

    std::memcpy(bus.ram.data() + 0x0300, kProgram, sizeof kProgram);
    bus.ram[0x02] = halfSteps;
    bus.ram[0xfffc] = 0x00;
    bus.ram[0xfffd] = 0x03;

    cpu.reset();
    for (long long c = 0; c < 20'000'000 && cpu.pc() != kDoneAddr;)
        c += cpu.run(1000);

    if (cpu.pc() != kDoneAddr) {
        std::printf("FAIL: program did not finish (pc=$%04x)\n", cpu.pc());
        ++failures;
        return {};
    }
    return std::vector<u8>(bus.ram.begin() + 0x1000, bus.ram.begin() + 0x3000);
}

void checkTrack(const std::vector<u8>& nibs, const std::vector<u8>& img,
                int expectTrack, const char* label) {
    auto found = scanNibbles(nibs);
    bool seen[16] = {};
    int good = 0;
    for (const auto& s : found) {
        if (s.track != expectTrack) {
            std::printf("FAIL %s: sector reports track %d, want %d\n", label,
                        s.track, expectTrack);
            ++failures;
            return;
        }
        if (s.sector > 15 || seen[s.sector]) continue;
        seen[s.sector] = true;
        const u8* want =
            img.data() + (expectTrack * 16 + kDosPhysToLog[s.sector]) * 256;
        if (std::memcmp(s.data.data(), want, 256) != 0) {
            std::printf("FAIL %s: track %d sector %d data mismatch\n", label,
                        expectTrack, s.sector);
            ++failures;
            return;
        }
        ++good;
    }
    if (good != 16) {
        std::printf("FAIL %s: recovered %d/16 sectors of track %d\n", label,
                    good, expectTrack);
        ++failures;
        return;
    }
    std::printf("PASS %s (16/16 sectors of track %d via the 6502 read loop)\n",
                label, expectTrack);
}

} // namespace

int main() {
    const auto img = makeSectorImage();
    const fs::path dir = fs::temp_directory_path() / "applebox_disk2_test";
    fs::create_directories(dir);
    const fs::path diskPath = dir / "test.dsk";
    {
        std::ofstream out(diskPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(img.data()),
                  static_cast<std::streamsize>(img.size()));
    }

    // --- Track 0, then seek to tracks 2 and 17 (2 half-steps per track) ---
    {
        Disk2Controller card;
        auto nibs = runCapture(diskPath, 0, card);
        CHECK(card.motorOn());
        checkTrack(nibs, img, 0, "read track 0");
    }
    {
        Disk2Controller card;
        auto nibs = runCapture(diskPath, 4, card);
        CHECK(card.headTrack() == 2.0);
        checkTrack(nibs, img, 2, "seek + read track 2");
    }
    {
        Disk2Controller card;
        auto nibs = runCapture(diskPath, 34, card);
        CHECK(card.headTrack() == 17.0);
        checkTrack(nibs, img, 17, "seek + read track 17");
    }

    // --- Controller-level checks -------------------------------------------
    {
        Disk2Controller card;
        std::string err;
        CHECK(card.insertDisk(0, diskPath, &err));
        card.reset();

        // Nothing assembles while the motor is off.
        card.io(0x8, false, 0, 0);            // motor off
        card.io(0xe, false, 0, 0);            // Q7L: read mode
        u8 v = card.io(0xc, false, 0, 100000);
        CHECK((v & 0x80) == 0);

        // With the motor on, nibbles arrive at roughly one per 32 cycles
        // (8 bit cells of 4 us), matching a 300 RPM drive.
        card.io(0x9, false, 0, 200000);
        s64 t = 200000;
        int nibbles = 0;
        for (int i = 0; i < 20000 && nibbles < 100; ++i) {
            t += 4;
            if (card.io(0xc, false, 0, t) & 0x80) ++nibbles;
        }
        const double cyclesPerNibble = static_cast<double>(t - 200000) / 100.0;
        CHECK(cyclesPerNibble > 30.0 && cyclesPerNibble < 42.0);
        std::printf("PASS read pacing (%.1f cycles per nibble)\n",
                    cyclesPerNibble);

        // A half-track between two tracks is unformatted on a standard disk.
        card.io(0x3, false, 0, t); // phase 1 on -> half-track step
        card.io(0x2, false, 0, t);
        CHECK(card.headTrack() == 0.5);
        int assembled = 0;
        for (int i = 0; i < 5000; ++i) {
            t += 4;
            if (card.io(0xc, false, 0, t) & 0x80) ++assembled;
        }
        CHECK(assembled == 0);
        std::printf("PASS half-track reads as unformatted\n");

        // Write-protect sense (Q6H + Q7L) reflects the image.
        card.io(0xe, false, 0, t);
        CHECK((card.io(0xd, false, 0, t) & 0x80) == 0); // .dsk is writable
    }

    fs::remove_all(dir);
    std::printf(failures ? "%d failure(s)\n" : "all Disk II tests passed\n",
                failures);
    return failures ? 1 : 0;
}
