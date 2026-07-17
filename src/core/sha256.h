// AppleBox — SHA-256 (FIPS 180-4). Implemented from the public specification.
// SPDX-License-Identifier: MIT
#pragma once

#include <span>
#include <string>

#include "core/types.h"

namespace ab {

// Returns lowercase hex digest of `data`.
std::string sha256Hex(std::span<const u8> data);

} // namespace ab
