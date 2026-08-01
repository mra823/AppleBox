// AppleBox — speaker DSP tests (Phase 2f).
// Measures the rendered waveform rather than inspecting internals: sample
// rate, square-wave frequency via zero crossings, amplitude, DC blocking,
// and that splitting a run into many small advances is equivalent to one
// large one (the emulator advances the speaker at arbitrary slice
// boundaries). No ROM or audio device is needed, so this runs in CI.
// SPDX-License-Identifier: MIT
#include <cmath>
#include <cstdio>
#include <vector>

#include "audio/speaker.h"

using ab::Speaker;
using ab::Ticks;

namespace {

int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

constexpr double kCpuHz = 1'020'484.0;
constexpr int kRate = 44100;

// Drains everything currently rendered.
std::vector<float> drain(Speaker& s) {
    std::vector<float> out(s.available());
    if (!out.empty()) out.resize(s.read(out.data(), out.size()));
    return out;
}

int zeroCrossings(const std::vector<float>& v, float threshold) {
    int crossings = 0;
    int state = 0; // -1 low, +1 high, 0 undecided
    for (float x : v) {
        if (x > threshold && state <= 0) {
            if (state != 0) ++crossings;
            state = 1;
        } else if (x < -threshold && state >= 0) {
            if (state != 0) ++crossings;
            state = -1;
        }
    }
    return crossings;
}

float rms(const std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    double sum = 0.0;
    for (float x : v) sum += static_cast<double>(x) * x;
    return static_cast<float>(std::sqrt(sum / v.size()));
}

// Toggles the speaker at `hz` for `seconds`, draining as it goes so the
// half-second ring never overruns.
std::vector<float> renderSquare(Speaker& s, double hz, double seconds) {
    const Ticks halfPeriod = static_cast<Ticks>(kCpuHz / (hz * 2.0));
    const Ticks end = static_cast<Ticks>(kCpuHz * seconds);
    std::vector<float> all;
    for (Ticks t = halfPeriod; t < end; t += halfPeriod) {
        s.toggle(t);
        if (s.available() > 8192) {
            auto chunk = drain(s);
            all.insert(all.end(), chunk.begin(), chunk.end());
        }
    }
    s.advanceTo(end);
    auto chunk = drain(s);
    all.insert(all.end(), chunk.begin(), chunk.end());
    return all;
}

} // namespace

int main() {
    // --- Sample rate: advancing N cycles yields N / cyclesPerSample frames --
    {
        Speaker s(kCpuHz, kRate);
        s.advanceTo(static_cast<Ticks>(kCpuHz / 10)); // 0.1 s
        const std::size_t got = s.available();
        const std::size_t want = kRate / 10;
        CHECK(got + 1 >= want && got <= want + 1);
        std::printf("PASS sample rate (%zu frames for 0.1 s, want ~%zu)\n", got,
                    want);
    }

    // --- Silence: no toggles means no signal ------------------------------
    {
        Speaker s(kCpuHz, kRate);
        s.advanceTo(static_cast<Ticks>(kCpuHz / 4));
        auto v = drain(s);
        CHECK(!v.empty());
        CHECK(rms(v) < 1e-6f);
        std::printf("PASS silence (rms %.2e)\n", rms(v));
    }

    // --- 1 kHz square wave: frequency and amplitude ------------------------
    {
        Speaker s(kCpuHz, kRate);
        s.setVolume(1.0f);
        auto v = renderSquare(s, 1000.0, 0.5);
        // Two crossings per cycle => ~1000 cycles over 0.5 s => ~1000 crossings.
        const int crossings = zeroCrossings(v, 0.1f);
        const double hz = crossings / 2.0 / 0.5;
        CHECK(hz > 980.0 && hz < 1020.0);
        CHECK(rms(v) > 0.2f);
        std::printf("PASS 1 kHz square (measured %.1f Hz, rms %.3f)\n", hz,
                    rms(v));
    }

    // --- A higher tone still tracks ---------------------------------------
    {
        Speaker s(kCpuHz, kRate);
        s.setVolume(1.0f);
        auto v = renderSquare(s, 4000.0, 0.25);
        const double hz = zeroCrossings(v, 0.1f) / 2.0 / 0.25;
        CHECK(hz > 3900.0 && hz < 4100.0);
        std::printf("PASS 4 kHz square (measured %.1f Hz)\n", hz);
    }

    // --- DC blocking: a level held high decays to silence -------------------
    {
        Speaker s(kCpuHz, kRate);
        s.setVolume(1.0f);
        s.toggle(10);                                   // level goes high
        s.advanceTo(static_cast<Ticks>(kCpuHz / 2));    // hold for 0.5 s
        auto v = drain(s);
        CHECK(v.size() > 1000);
        // Starts as a step, ends near zero.
        CHECK(std::fabs(v.front()) > 0.5f);
        CHECK(std::fabs(v.back()) < 0.01f);
        std::printf("PASS DC blocking (step %.3f -> tail %.4f)\n", v.front(),
                    v.back());
    }

    // --- Slice independence: many small advances == one big advance --------
    {
        Speaker a(kCpuHz, kRate), b(kCpuHz, kRate);
        a.setVolume(1.0f);
        b.setVolume(1.0f);
        const Ticks halfPeriod = static_cast<Ticks>(kCpuHz / 2000.0);
        const Ticks end = static_cast<Ticks>(kCpuHz / 10);

        for (Ticks t = halfPeriod; t < end; t += halfPeriod) a.toggle(t);
        a.advanceTo(end);

        // Same toggles, but with the machine advancing every 137 cycles.
        Ticks next = halfPeriod;
        for (Ticks t = 137; t <= end; t += 137) {
            while (next < t && next < end) {
                b.toggle(next);
                next += halfPeriod;
            }
            b.advanceTo(t);
        }
        while (next < end) {
            b.toggle(next);
            next += halfPeriod;
        }
        b.advanceTo(end);

        auto va = drain(a), vb = drain(b);
        CHECK(va.size() == vb.size());
        float worst = 0.0f;
        for (std::size_t i = 0; i < std::min(va.size(), vb.size()); ++i)
            worst = std::max(worst, std::fabs(va[i] - vb[i]));
        CHECK(worst < 1e-5f);
        std::printf("PASS slice independence (%zu frames, max diff %.2e)\n",
                    va.size(), worst);
    }

    // --- Overrun handling: a consumer that never reads bounds latency ------
    {
        Speaker s(kCpuHz, kRate);
        // A single huge advance is clamped by kMaxCatchUpSeconds, so step in
        // realistic slices to fill the ring past its half-second capacity.
        for (int i = 1; i <= 10; ++i)
            s.advanceTo(static_cast<Ticks>(kCpuHz * i / 10.0)); // 1 s total
        CHECK(s.available() <= static_cast<std::size_t>(kRate / 2));
        CHECK(s.overruns() > 0);
        std::printf("PASS overrun bounded (%zu frames buffered, %llu dropped)\n",
                    s.available(),
                    static_cast<unsigned long long>(s.overruns()));
    }

    // --- Catch-up clamp: a long pause does not flood the buffer ------------
    {
        Speaker s(kCpuHz, kRate);
        s.advanceTo(static_cast<Ticks>(kCpuHz * 30)); // 30 s "paused"
        CHECK(s.available() <=
              static_cast<std::size_t>(Speaker::kMaxCatchUpSeconds * kRate) + 1);
        std::printf("PASS catch-up clamp (%zu frames after a 30 s gap)\n",
                    s.available());
    }

    // --- Mute silences the output but keeps timing -------------------------
    {
        Speaker s(kCpuHz, kRate);
        s.setVolume(1.0f);
        s.setMuted(true);
        auto v = renderSquare(s, 1000.0, 0.1);
        CHECK(!v.empty());
        CHECK(rms(v) == 0.0f);
        std::printf("PASS mute (%zu silent frames)\n", v.size());
    }

    std::printf(failures ? "%d failure(s)\n" : "all speaker tests passed\n",
                failures);
    return failures ? 1 : 0;
}
