// AppleBox — ROM manifest loader. 86Box-style rom_present() gate: each machine
// declares required ROM files (name, size, sha256); a machine is selectable
// only when all required ROMs are present and valid under roms/<machine>/.
// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/types.h"

namespace ab {

struct RomFile {
    std::string filename;   // relative to roms/<machineDir>/
    std::size_t size = 0;   // required exact size in bytes
    std::string sha256;     // lowercase hex; empty = size check only
    bool optional = false;  // e.g. Apple 1 BASIC ROM
};

struct RomCheckResult {
    bool available = false;              // all required ROMs present + valid
    std::vector<std::string> missing;    // required files absent
    std::vector<std::string> badHash;    // present but wrong size/hash
};

// Verify a machine's ROM set under `romRoot`/`machineDir`.
RomCheckResult checkRoms(const std::filesystem::path& romRoot,
                         const std::string& machineDir,
                         const std::vector<RomFile>& files);

// Load one ROM file, validating size (and hash when specified).
// Returns std::nullopt on missing file or validation failure.
std::optional<std::vector<u8>> loadRom(const std::filesystem::path& romRoot,
                                       const std::string& machineDir,
                                       const RomFile& file);

} // namespace ab
