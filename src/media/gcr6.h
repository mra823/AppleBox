// AppleBox — Apple 5.25" GCR (6&2 and 4&4) encoding.
// The Disk II records data as "nibbles": bytes with the high bit set and no
// more than two consecutive zero bits, so the drive's shift register can
// self-synchronise. 6&2 packs 256 data bytes into 342 six-bit groups plus a
// checksum; 4&4 splits a byte across two nibbles for address fields.
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "core/types.h"

namespace ab::media {

// 6&2: 256 data bytes -> 343 nibbles (342 + checksum).
constexpr std::size_t kSectorNibbles62 = 343;

std::vector<u8> encodeSector62(std::span<const u8> data /*256*/);

// Decodes 343 nibbles back to 256 bytes. Returns false on an invalid nibble
// or checksum mismatch.
bool decodeSector62(std::span<const u8> nibbles, std::array<u8, 256>& out);

// 4&4: one byte -> two nibbles (odd bits, even bits).
constexpr u8 encode44Odd(u8 v) { return static_cast<u8>((v >> 1) | 0xaa); }
constexpr u8 encode44Even(u8 v) { return static_cast<u8>(v | 0xaa); }
constexpr u8 decode44(u8 odd, u8 even) {
    return static_cast<u8>(((odd << 1) | 0x01) & even);
}

} // namespace ab::media
