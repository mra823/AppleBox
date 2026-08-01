// AppleBox — Apple II speaker (1-bit click generator).
// $C030 toggles the speaker cone. Sound quality depends entirely on
// resampling that square wave properly: each output sample is the average
// level over its window (a box filter, which band-limits the 1 MHz-resolution
// toggle stream down to the audio rate), followed by a DC blocker so long
// runs at one level decay to silence instead of pinning the waveform.
//
// This class is pure DSP with no audio backend, so it is unit-testable.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <vector>

#include "core/types.h"

namespace ab {

class StateVisitor;

class Speaker {
public:
    static constexpr int kDefaultSampleRate = 44100;
    // Ignore gaps longer than this (the machine was paused or stepped);
    // otherwise a single advance could emit minutes of samples.
    static constexpr double kMaxCatchUpSeconds = 0.25;

    Speaker(double cpuHz = 1'020'484.0, int sampleRate = kDefaultSampleRate);

    // $C030 access at the given CPU cycle.
    void toggle(Ticks cycle);
    // Integrate up to `cycle` without changing the level. Call this at least
    // once per frame so silence is produced and the output stays in step.
    void advanceTo(Ticks cycle);

    // Drain rendered samples; returns how many frames were written.
    std::size_t read(float* out, std::size_t frames);
    std::size_t available() const { return count_; }
    void clear();
    void reset(Ticks cycle = 0);

    void setVolume(float v) { volume_ = v; }
    float volume() const { return volume_; }
    void setMuted(bool m) { muted_ = m; }
    bool muted() const { return muted_; }

    int sampleRate() const { return sampleRate_; }
    double cyclesPerSample() const { return cyclesPerSample_; }
    // Samples dropped because the consumer fell behind (diagnostics).
    u64 overruns() const { return overruns_; }

    void serialize(StateVisitor& v);

private:
    void emit(float sample);

    double cyclesPerSample_ = 0.0;
    int sampleRate_ = kDefaultSampleRate;
    Ticks lastCycle_ = 0;
    bool level_ = false;

    double accum_ = 0.0;       // level integrated over the current window
    double windowCycles_ = 0.0;

    float dcX_ = 0.0f, dcY_ = 0.0f; // DC blocker history
    float volume_ = 0.5f;
    bool muted_ = false;

    std::vector<float> ring_;
    std::size_t head_ = 0, tail_ = 0, count_ = 0;
    u64 overruns_ = 0;
};

} // namespace ab
