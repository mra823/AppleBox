// AppleBox — core type aliases and constants.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace ab {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

// Master-clock ticks. 64-bit; every device clock is expressed as a rational
// ratio of this timeline (86Box-style 64-bit master clock).
using Ticks = s64;

} // namespace ab
