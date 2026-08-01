// AppleBox — Apple II speaker.
// SPDX-License-Identifier: MIT
#include "audio/speaker.h"

#include <algorithm>
#include <cmath>

#include "core/savestate.h"

namespace ab {

namespace {
// One-pole DC blocker. 0.999 at 44.1 kHz puts the corner near 7 Hz, which
// removes the offset without audibly thinning square waves.
constexpr float kDcPole = 0.999f;
} // namespace

Speaker::Speaker(double cpuHz, int sampleRate)
    : cyclesPerSample_(cpuHz / static_cast<double>(sampleRate)),
      sampleRate_(sampleRate),
      // Half a second of slack absorbs frame-time jitter.
      ring_(static_cast<std::size_t>(sampleRate / 2)) {}

void Speaker::toggle(Ticks cycle) {
    advanceTo(cycle);
    level_ = !level_;
}

void Speaker::advanceTo(Ticks cycle) {
    if (cycle <= lastCycle_) {
        lastCycle_ = cycle; // reset or rewind: resynchronise silently
        return;
    }
    double remaining = static_cast<double>(cycle - lastCycle_);
    lastCycle_ = cycle;

    const double maxCatchUp =
        kMaxCatchUpSeconds * cyclesPerSample_ * sampleRate_;
    if (remaining > maxCatchUp) remaining = maxCatchUp;

    const double levelValue = level_ ? 1.0 : 0.0;
    while (remaining > 0.0) {
        const double need = cyclesPerSample_ - windowCycles_;
        if (remaining < need) {
            accum_ += remaining * levelValue;
            windowCycles_ += remaining;
            break;
        }
        accum_ += need * levelValue;
        remaining -= need;
        emit(static_cast<float>(accum_ / cyclesPerSample_));
        accum_ = 0.0;
        windowCycles_ = 0.0;
    }
}

void Speaker::emit(float sample) {
    // Remove the DC offset so a steady level decays to silence.
    const float y = sample - dcX_ + kDcPole * dcY_;
    dcX_ = sample;
    dcY_ = y;

    const float out = muted_ ? 0.0f : std::clamp(y * volume_, -1.0f, 1.0f);

    ring_[head_] = out;
    head_ = (head_ + 1) % ring_.size();
    if (count_ == ring_.size()) {
        // Consumer fell behind; drop the oldest frame to bound latency.
        tail_ = (tail_ + 1) % ring_.size();
        ++overruns_;
    } else {
        ++count_;
    }
}

std::size_t Speaker::read(float* out, std::size_t frames) {
    const std::size_t n = std::min(frames, count_);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = ring_[tail_];
        tail_ = (tail_ + 1) % ring_.size();
    }
    count_ -= n;
    return n;
}

void Speaker::clear() {
    head_ = tail_ = count_ = 0;
}

void Speaker::reset(Ticks cycle) {
    clear();
    lastCycle_ = cycle;
    level_ = false;
    accum_ = 0.0;
    windowCycles_ = 0.0;
    dcX_ = dcY_ = 0.0f;
    overruns_ = 0;
}

void Speaker::serialize(StateVisitor& v) {
    v.value("speaker.lastCycle", lastCycle_);
    v.value("speaker.level", level_);
    v.value("speaker.accum", accum_);
    v.value("speaker.windowCycles", windowCycles_);
}

} // namespace ab
