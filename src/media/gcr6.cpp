// AppleBox — Apple 5.25" GCR (6&2) encoding.
// SPDX-License-Identifier: MIT
#include "media/gcr6.h"

namespace ab::media {

namespace {

// Six-bit value -> disk nibble ("write translate table"). Every entry has the
// high bit set and no more than two consecutive zero bits.
constexpr u8 kWrite62[64] = {
    0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6, 0xa7, 0xab, 0xac,
    0xad, 0xae, 0xaf, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba,
    0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3, 0xd6,
    0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe5, 0xe6, 0xe7,
    0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5,
    0xf6, 0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};

// Inverse table, built once; 0xff marks an invalid nibble.
struct ReadTable {
    u8 t[256];
    constexpr ReadTable() : t{} {
        for (int i = 0; i < 256; ++i) t[i] = 0xff;
        for (int i = 0; i < 64; ++i) t[kWrite62[i]] = static_cast<u8>(i);
    }
};
constexpr ReadTable kRead62{};

// Swap the two low bits (the 6&2 "aux" groups store them reversed).
constexpr u8 swap2(u8 v) {
    return static_cast<u8>(((v & 1) << 1) | ((v & 2) >> 1));
}

} // namespace

std::vector<u8> encodeSector62(std::span<const u8> data) {
    std::vector<u8> out;
    if (data.size() < 256) return out;
    out.reserve(kSectorNibbles62);

    // 342 six-bit groups: 86 "aux" groups of packed low bits, then the 256
    // high-six-bit groups.
    u8 buf[342] = {};
    for (int i = 0; i < 256; ++i) buf[86 + i] = static_cast<u8>(data[i] >> 2);
    for (int i = 0; i < 86; ++i) {
        u8 v = swap2(data[i]);
        if (i + 86 < 256) v |= static_cast<u8>(swap2(data[i + 86]) << 2);
        if (i + 172 < 256) v |= static_cast<u8>(swap2(data[i + 172]) << 4);
        buf[i] = v;
    }

    // XOR chain, then the running checksum as the final nibble.
    u8 last = 0;
    for (u8 v : buf) {
        out.push_back(kWrite62[(v ^ last) & 0x3f]);
        last = v;
    }
    out.push_back(kWrite62[last & 0x3f]);
    return out;
}

bool decodeSector62(std::span<const u8> nibbles, std::array<u8, 256>& out) {
    if (nibbles.size() < kSectorNibbles62) return false;

    u8 buf[342];
    u8 last = 0;
    for (std::size_t i = 0; i < 342; ++i) {
        const u8 six = kRead62.t[nibbles[i]];
        if (six == 0xff) return false;
        last ^= six;
        buf[i] = last;
    }
    const u8 checksum = kRead62.t[nibbles[342]];
    if (checksum == 0xff || checksum != last) return false;

    for (int i = 0; i < 256; ++i) {
        u8 aux = buf[i % 86];
        const int group = i / 86; // 0, 1 or 2
        const u8 lowBits = swap2(static_cast<u8>((aux >> (group * 2)) & 3));
        out[i] = static_cast<u8>((buf[86 + i] << 2) | lowBits);
    }
    return true;
}

} // namespace ab::media
