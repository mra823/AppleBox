// SPDX-License-Identifier: MIT
#include "core/sha256.h"

#include <array>
#include <bit>
#include <cstring>

namespace ab {
namespace {

constexpr std::array<u32, 64> K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

void compress(std::array<u32, 8>& h, const u8* block) {
    u32 w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = static_cast<u32>(block[i * 4]) << 24 | static_cast<u32>(block[i * 4 + 1]) << 16 |
               static_cast<u32>(block[i * 4 + 2]) << 8 | block[i * 4 + 3];
    for (int i = 16; i < 64; ++i) {
        u32 s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        u32 s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        u32 s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        u32 ch = (e & f) ^ (~e & g);
        u32 t1 = hh + s1 + ch + K[static_cast<std::size_t>(i)] + w[i];
        u32 s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        u32 maj = (a & b) ^ (a & c) ^ (b & c);
        u32 t2 = s0 + maj;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

} // namespace

std::string sha256Hex(std::span<const u8> data) {
    std::array<u32, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::size_t full = data.size() / 64;
    for (std::size_t i = 0; i < full; ++i) compress(h, data.data() + i * 64);

    // Final padded block(s).
    u8 tail[128] = {};
    std::size_t rem = data.size() - full * 64;
    if (rem) std::memcpy(tail, data.data() + full * 64, rem);
    tail[rem] = 0x80;
    std::size_t tailLen = (rem < 56) ? 64 : 128;
    u64 bitLen = static_cast<u64>(data.size()) * 8;
    for (int i = 0; i < 8; ++i)
        tail[tailLen - 1 - static_cast<std::size_t>(i)] = static_cast<u8>(bitLen >> (i * 8));
    compress(h, tail);
    if (tailLen == 128) compress(h, tail + 64);

    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (u32 word : h)
        for (int shift = 28; shift >= 0; shift -= 4)
            out.push_back(hex[(word >> shift) & 0xF]);
    return out;
}

} // namespace ab
