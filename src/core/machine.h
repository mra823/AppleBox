// AppleBox — machine registry. 86Box-style: machines are grouped by CPU
// family (the UI's category dropdown) and gated by ROM availability.
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

#include "core/rom_manifest.h"
#include "core/types.h"

namespace ab {

// UI category dropdown ordering mirrors the plan's machine-type organization.
enum class CpuFamily {
    M6502,
    M65C816,
    M68000,
    M68020,
    M68030,
    M68040,
    PPC601,
    PPC603_604,
    PPC750,
};

const char* cpuFamilyName(CpuFamily f);

struct MachineDesc {
    std::string id;        // stable config key, e.g. "apple1"
    std::string name;      // display name, e.g. "Apple I"
    CpuFamily family = CpuFamily::M6502;
    std::string romDir;    // subfolder under roms/
    std::vector<RomFile> roms;
    int phase = 0;         // implementation phase from the plan (0 = not yet)
};

// Full planned catalog (unimplemented machines carry their plan phase and are
// shown greyed-out in the UI). ROM manifests are filled in per phase.
const std::vector<MachineDesc>& machineCatalog();

// 86Box available() gate: true when the machine is implemented (phase > 0 has
// begun) and its required ROMs validate under romRoot.
bool machineAvailable(const MachineDesc& m, const std::filesystem::path& romRoot);

} // namespace ab
