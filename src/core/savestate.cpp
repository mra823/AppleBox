// SPDX-License-Identifier: MIT
#include "core/savestate.h"

namespace ab {
namespace {

void putU32(std::vector<u8>& out, u32 v) {
    out.push_back(static_cast<u8>(v));
    out.push_back(static_cast<u8>(v >> 8));
    out.push_back(static_cast<u8>(v >> 16));
    out.push_back(static_cast<u8>(v >> 24));
}

bool getU32(std::span<const u8> in, std::size_t& pos, u32& v) {
    if (pos + 4 > in.size()) return false;
    v = static_cast<u32>(in[pos]) | static_cast<u32>(in[pos + 1]) << 8 |
        static_cast<u32>(in[pos + 2]) << 16 | static_cast<u32>(in[pos + 3]) << 24;
    pos += 4;
    return true;
}

constexpr u8 kMagic[4] = {'A', 'B', 'S', 'S'}; // AppleBox SaveState

} // namespace

std::vector<u8> StateSnapshot::toBytes() const {
    std::vector<u8> out;
    out.insert(out.end(), kMagic, kMagic + 4);
    putU32(out, kFormatVersion);
    putU32(out, static_cast<u32>(entries_.size()));
    for (const auto& [key, data] : entries_) {
        putU32(out, static_cast<u32>(key.size()));
        out.insert(out.end(), key.begin(), key.end());
        putU32(out, static_cast<u32>(data.size()));
        out.insert(out.end(), data.begin(), data.end());
    }
    return out;
}

bool StateSnapshot::fromBytes(std::span<const u8> bytes, StateSnapshot& out) {
    std::size_t pos = 0;
    if (bytes.size() < 4 || std::memcmp(bytes.data(), kMagic, 4) != 0) return false;
    pos = 4;
    u32 version = 0, count = 0;
    if (!getU32(bytes, pos, version) || version != kFormatVersion) return false;
    if (!getU32(bytes, pos, count)) return false;
    out.entries_.clear();
    for (u32 i = 0; i < count; ++i) {
        u32 keyLen = 0;
        if (!getU32(bytes, pos, keyLen) || pos + keyLen > bytes.size()) return false;
        std::string key(reinterpret_cast<const char*>(bytes.data() + pos), keyLen);
        pos += keyLen;
        u32 dataLen = 0;
        if (!getU32(bytes, pos, dataLen) || pos + dataLen > bytes.size()) return false;
        out.entries_[key].assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                                 bytes.begin() + static_cast<std::ptrdiff_t>(pos + dataLen));
        pos += dataLen;
    }
    return pos == bytes.size();
}

} // namespace ab
