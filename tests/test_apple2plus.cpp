// AppleBox — Apple II+ machine tests (Phase 2a).
// With no card in slot 6, boots the user-supplied Autostart/Applesoft ROM to
// the "]" prompt and types PRINT 2+2. With the Disk II card installed and no
// disk in the drive, checks the machine does what real hardware does: spin the
// drive and wait, never reaching BASIC. Skips (77) when the ROM is absent.
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "machines/apple2plus.h"

namespace {

bool screenContains(ab::Apple2PlusMachine& m, const std::string& needle) {
    for (const auto& line : m.textScreen())
        if (line.find(needle) != std::string::npos) return true;
    return false;
}

// The Applesoft prompt starts a line. Searching the whole screen for "]"
// would also match the "APPLE ][" banner, so anchor it to column 0.
bool hasPromptLine(ab::Apple2PlusMachine& m) {
    for (const auto& line : m.textScreen())
        if (!line.empty() && line[0] == ']') return true;
    return false;
}

void dumpScreen(ab::Apple2PlusMachine& m) {
    for (const auto& line : m.textScreen()) std::printf("|%s|\n", line.c_str());
}

// Measures the tone the machine produced: its length between the first and
// last polarity change, and its frequency from the crossing count. The
// region after the final toggle is the DC blocker decaying, not sound, so it
// is excluded from the timing.
struct Tone {
    double ms = 0.0;
    double hz = 0.0;
    double peak = 0.0;
};

Tone measureTone(const std::vector<float>& v, double sampleRate) {
    int crossings = 0, state = 0;
    std::size_t firstX = 0, lastX = 0;
    Tone t;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const int prev = state;
        if (v[i] > 0.05f && state <= 0) {
            if (state) ++crossings;
            state = 1;
        } else if (v[i] < -0.05f && state >= 0) {
            if (state) ++crossings;
            state = -1;
        }
        if (state != prev && prev != 0) {
            if (firstX == 0) firstX = i;
            lastX = i;
        }
        t.peak = std::max<double>(t.peak, std::fabs(v[i]));
    }
    if (lastX <= firstX) return t;
    const double secs = (lastX - firstX) / sampleRate;
    t.ms = secs * 1000.0;
    t.hz = crossings / 2.0 / secs;
    return t;
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path romRoot = "roms";
    if (argc > 1) romRoot = argv[1];
    else if (const char* env = std::getenv("APPLEBOX_ROM_DIR")) romRoot = env;

    ab::Apple2PlusMachine m;
    if (!m.loadRoms(romRoot)) {
        std::printf("SKIP: roms/apple2plus/apple2plus.rom not present\n");
        return 77;
    }
    // A II+ with a Disk II card boots the drive, so test the bare machine.
    m.disk2().removeCard();

    m.reset();
    m.run(4'000'000); // cold start: banner, slot scan, Applesoft entry

    if (!screenContains(m, "APPLE ][") || !hasPromptLine(m)) {
        std::printf("FAIL: no Applesoft prompt after cold start\n");
        dumpScreen(m);
        return 1;
    }
    std::printf("PASS Apple II+ cold start to \"]\" prompt\n");

    m.typeString("PRINT 2+2\r");
    m.run(2'000'000);

    if (!screenContains(m, "4")) {
        std::printf("FAIL: PRINT 2+2 produced no '4'\n");
        dumpScreen(m);
        return 1;
    }
    std::printf("PASS PRINT 2+2 -> 4\n");

    // --- Disk II installed, drive empty -----------------------------------
    // Real hardware hangs in the boot ROM with the drive turning; reaching
    // BASIC here would mean the card is not being seen at all.
    ab::Apple2PlusMachine d;
    if (!d.loadRoms(romRoot)) return 1;
    if (d.disk2().hasBootRom()) {
        d.reset();
        d.run(4'000'000);
        if (hasPromptLine(d)) {
            std::printf("FAIL: slot 6 card ignored; fell through to BASIC\n");
            dumpScreen(d);
            return 1;
        }
        if (!d.disk2().motorOn()) {
            std::printf("FAIL: boot ROM did not start the drive motor\n");
            return 1;
        }
        std::printf("PASS Disk II with no disk spins and waits in the boot ROM\n");
    } else {
        std::printf("SKIP disk-II-present case: roms/apple2plus/disk2.rom absent\n");
    }

    // --- Speaker: the monitor's BELL routine at cold start -----------------
    // BELL toggles $C030 exactly 192 times (LDY #$C0) around a WAIT delay,
    // giving roughly 1 kHz for a tenth of a second. Checking the rendered
    // waveform covers the whole chain: ROM timing, the softswitch, and the
    // speaker's resampling.
    {
        ab::Apple2PlusMachine s;
        if (!s.loadRoms(romRoot)) return 1;
        s.disk2().removeCard();
        s.speaker().setVolume(1.0f);
        s.reset();

        std::vector<float> audio;
        for (int i = 0; i < 180; ++i) { // 3 s in frame-sized slices
            s.run(ab::Apple2PlusMachine::kClockHz / 60);
            std::vector<float> buf(s.speaker().available());
            if (!buf.empty()) {
                buf.resize(s.speaker().read(buf.data(), buf.size()));
                audio.insert(audio.end(), buf.begin(), buf.end());
            }
        }

        const Tone t = measureTone(audio, s.speaker().sampleRate());
        if (s.speakerToggles() != 192) {
            std::printf("FAIL: BELL made %llu speaker toggles, want 192\n",
                        static_cast<unsigned long long>(s.speakerToggles()));
            return 1;
        }
        if (t.hz < 850.0 || t.hz > 1050.0 || t.ms < 80.0 || t.ms > 120.0 ||
            t.peak < 0.5) {
            std::printf("FAIL: bell tone %.0f Hz for %.1f ms peak %.2f; want "
                        "~1 kHz for ~100 ms\n",
                        t.hz, t.ms, t.peak);
            return 1;
        }
        std::printf("PASS speaker bell (%.0f Hz, %.1f ms, peak %.2f, 192 "
                    "toggles)\n",
                    t.hz, t.ms, t.peak);
    }
    return 0;
}
