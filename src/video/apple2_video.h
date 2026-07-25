// AppleBox — Apple II video renderer (Phase 2b).
// Renders the active video mode from machine RAM into a 280×192 RGBA8888
// framebuffer: 40-column text (inverse/flash), lores, hires with bit-pair
// NTSC artifact color, and mixed mode. Whole-frame rendering; cycle-exact
// beam effects are a later refinement (see plan thresholds).
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "core/types.h"

namespace ab {

struct Apple2VideoState {
    bool text = true;
    bool mixed = false;
    bool page2 = false;
    bool hires = false;
    bool flash = false; // flash-phase state (~2 Hz square, driven by caller)
};

class Apple2Video {
public:
    static constexpr int kWidth = 280;
    static constexpr int kHeight = 192;

    Apple2Video() : fb_(kWidth * kHeight, 0xff000000u) {}

    // `ram` is the machine's $0000-$BFFF address space.
    void render(std::span<const u8> ram, const Apple2VideoState& st);

    const std::vector<u32>& framebuffer() const { return fb_; } // ABGR8888
    u64 frameHash() const; // FNV-1a 64 over the framebuffer

private:
    void renderTextRow(std::span<const u8> ram, const Apple2VideoState& st,
                       int row);
    void renderLoresRow(std::span<const u8> ram, const Apple2VideoState& st,
                        int row);
    void renderHiresRow(std::span<const u8> ram, const Apple2VideoState& st,
                        int row);

    std::vector<u32> fb_;
};

} // namespace ab
