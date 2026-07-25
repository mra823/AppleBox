// AppleBox — Apple II video frame-hash regression tests (Phase 2b).
// Renders synthetic text/lores/hires/mixed frames from poked RAM (no Apple
// ROM required) and compares FNV-1a 64 hashes against pinned references.
// If a rendering change is intentional, re-pin the hashes printed on failure
// after visually verifying the output in the UI.
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "video/apple2_video.h"

using ab::Apple2Video;
using ab::Apple2VideoState;
using ab::u16;
using ab::u64;
using ab::u8;

namespace {

int failures = 0;

u16 textRowBase(int row) {
    return static_cast<u16>(0x0400 + (row % 8) * 0x80 + (row / 8) * 0x28);
}

u16 hiresLineBase(int line) {
    return static_cast<u16>(0x2000 + (line % 8) * 0x400 +
                            ((line / 8) % 8) * 0x80 + (line / 64) * 0x28);
}

void check(const char* name, u64 got, u64 want) {
    if (got != want) {
        std::printf("FAIL %s: hash %016llx, want %016llx\n", name,
                    static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
        ++failures;
    } else {
        std::printf("PASS %s (%016llx)\n", name,
                    static_cast<unsigned long long>(got));
    }
}

// Screen code for a normal (non-inverse) character.
u8 sc(char c) { return static_cast<u8>(c) | 0x80; }

} // namespace

int main() {
    std::vector<u8> ram(0xC000, 0);
    Apple2Video video;

    // --- Text mode: banner, inverse and flash characters ------------------
    {
        std::memset(ram.data() + 0x0400, 0xa0, 0x0400); // spaces
        const char* msg = "APPLE ][ VIDEO TEST 0123456789 ?!@#$";
        u16 base = textRowBase(0);
        for (int i = 0; msg[i]; ++i) ram[base + i] = sc(msg[i]);
        base = textRowBase(11);
        for (int i = 0; i < 40; ++i) ram[base + i] = static_cast<u8>(i);       // inverse
        base = textRowBase(23);
        for (int i = 0; i < 40; ++i) ram[base + i] = static_cast<u8>(0x40 + i); // flash

        Apple2VideoState st;
        st.text = true;
        video.render(ram, st);
        check("text", video.frameHash(), 0x2b9d08e1a0147e1dull);
        st.flash = true;
        video.render(ram, st);
        check("text-flash", video.frameHash(), 0xd888cb5e30f8425dull);
    }

    // --- Lores: all 16 colors + checkerboard -------------------------------
    {
        std::memset(ram.data() + 0x0400, 0xa0, 0x0400);
        for (int row = 0; row < 24; ++row) {
            u16 base = textRowBase(row);
            for (int col = 0; col < 40; ++col) {
                u8 lo = static_cast<u8>((col + row) & 0x0f);
                u8 hi = static_cast<u8>((col * 3 + row) & 0x0f);
                ram[base + col] = static_cast<u8>(lo | (hi << 4));
            }
        }
        Apple2VideoState st;
        st.text = false;
        st.hires = false;
        video.render(ram, st);
        check("lores", video.frameHash(), 0x8f274ec57591403dull);
    }

    // --- Hires: isolated dots, runs, and palette-shift bytes ---------------
    {
        std::memset(ram.data() + 0x2000, 0, 0x2000);
        for (int line = 0; line < 192; ++line) {
            u16 base = hiresLineBase(line);
            for (int byte = 0; byte < 40; ++byte) {
                u8 v = 0;
                switch ((line / 8 + byte) & 3) {
                    case 0: v = 0x2a; break;           // isolated dots, even
                    case 1: v = 0x55; break;           // isolated dots, odd
                    case 2: v = 0xd5; break;           // odd + palette shift
                    case 3: v = 0x7f; break;           // solid run -> white
                }
                ram[base + byte] = v;
            }
        }
        Apple2VideoState st;
        st.text = false;
        st.hires = true;
        video.render(ram, st);
        check("hires", video.frameHash(), 0x23973783ceabf205ull);
    }

    // --- Mixed: hires top, text rows 20-23 ---------------------------------
    {
        u16 base = textRowBase(20);
        const char* msg = "MIXED MODE";
        for (int i = 0; msg[i]; ++i) ram[base + i] = sc(msg[i]);
        Apple2VideoState st;
        st.text = false;
        st.hires = true;
        st.mixed = true;
        video.render(ram, st);
        check("mixed", video.frameHash(), 0x56adf2e0d5cd27c4ull);
    }

    std::printf(failures ? "%d failure(s)\n" : "all video tests passed\n",
                failures);
    return failures ? 1 : 0;
}
