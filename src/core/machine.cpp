// SPDX-License-Identifier: MIT
#include "core/machine.h"

namespace ab {

const char* cpuFamilyName(CpuFamily f) {
    switch (f) {
        case CpuFamily::M6502: return "6502";
        case CpuFamily::M65C816: return "65C816";
        case CpuFamily::M68000: return "68000";
        case CpuFamily::M68020: return "68020";
        case CpuFamily::M68030: return "68030";
        case CpuFamily::M68040: return "68040";
        case CpuFamily::PPC601: return "PowerPC 601";
        case CpuFamily::PPC603_604: return "PowerPC 603/604";
        case CpuFamily::PPC750: return "PowerPC 750 (G3)";
    }
    return "?";
}

const std::vector<MachineDesc>& machineCatalog() {
    // ROM manifests are populated as each machine's phase is implemented.
    // Phase 0: catalog only; nothing is selectable yet.
    static const std::vector<MachineDesc> catalog = {
        // --- 6502 (Phases 1–2) ---
        {"apple1",
         "Apple I",
         CpuFamily::M6502,
         "apple1",
         {{"wozmon.rom", 0x100, "", false}, {"basic.rom", 0x1000, "", true}},
         1},
        {"apple2", "Apple II", CpuFamily::M6502, "apple2", {}, 0},
        {"apple2plus", "Apple II+", CpuFamily::M6502, "apple2plus", {}, 0},
        {"apple2e", "Apple IIe", CpuFamily::M6502, "apple2e", {}, 0},
        {"apple2ee", "Apple IIe Enhanced", CpuFamily::M6502, "apple2ee", {}, 0},
        {"apple2c", "Apple IIc", CpuFamily::M6502, "apple2c", {}, 0},
        {"apple2cplus", "Apple IIc+", CpuFamily::M6502, "apple2cplus", {}, 0},
        {"apple3", "Apple III", CpuFamily::M6502, "apple3", {}, 0},
        // --- 65C816 (Phase 4) ---
        {"apple2gs_rom00", "Apple IIGS (ROM 00)", CpuFamily::M65C816, "apple2gs", {}, 0},
        {"apple2gs_rom01", "Apple IIGS (ROM 01)", CpuFamily::M65C816, "apple2gs", {}, 0},
        {"apple2gs_rom03", "Apple IIGS (ROM 03)", CpuFamily::M65C816, "apple2gs", {}, 0},
        // --- 68000 (Phases 3, 5) ---
        {"lisa2", "Lisa 2", CpuFamily::M68000, "lisa2", {}, 0},
        {"mac128k", "Macintosh 128K", CpuFamily::M68000, "mac128k", {}, 0},
        {"mac512k", "Macintosh 512K", CpuFamily::M68000, "mac512k", {}, 0},
        {"macplus", "Macintosh Plus", CpuFamily::M68000, "macplus", {}, 0},
        {"macse", "Macintosh SE", CpuFamily::M68000, "macse", {}, 0},
        {"macclassic", "Macintosh Classic", CpuFamily::M68000, "macclassic", {}, 0},
        {"macportable", "Macintosh Portable", CpuFamily::M68000, "macportable", {}, 0},
        // --- 68020 (Phase 6) ---
        {"mac2", "Macintosh II", CpuFamily::M68020, "mac2", {}, 0},
        {"maclc", "Macintosh LC", CpuFamily::M68020, "maclc", {}, 0},
        // --- 68030 (Phase 6) ---
        {"mac2x", "Macintosh IIx", CpuFamily::M68030, "mac2x", {}, 0},
        {"mac2cx", "Macintosh IIcx", CpuFamily::M68030, "mac2cx", {}, 0},
        {"mac2ci", "Macintosh IIci", CpuFamily::M68030, "mac2ci", {}, 0},
        {"mac2si", "Macintosh IIsi", CpuFamily::M68030, "mac2si", {}, 0},
        {"macse30", "Macintosh SE/30", CpuFamily::M68030, "macse30", {}, 0},
        {"maclc3", "Macintosh LC III", CpuFamily::M68030, "maclc3", {}, 0},
        {"macclassic2", "Macintosh Classic II", CpuFamily::M68030, "macclassic2", {}, 0},
        // --- 68040 (Phase 6) ---
        {"quadra610", "Quadra/Centris 610", CpuFamily::M68040, "quadra610", {}, 0},
        {"quadra650", "Quadra/Centris 650", CpuFamily::M68040, "quadra650", {}, 0},
        {"quadra700", "Quadra 700", CpuFamily::M68040, "quadra700", {}, 0},
        {"quadra840av", "Quadra 840AV", CpuFamily::M68040, "quadra840av", {}, 0},
        {"maclc475", "Macintosh LC 475", CpuFamily::M68040, "maclc475", {}, 0},
        // --- PowerPC (Phase 7) ---
        {"pm6100", "Power Macintosh 6100", CpuFamily::PPC601, "pm6100", {}, 0},
        {"pm7100", "Power Macintosh 7100", CpuFamily::PPC601, "pm7100", {}, 0},
        {"pm8100", "Power Macintosh 8100", CpuFamily::PPC601, "pm8100", {}, 0},
        {"pm7200", "Power Macintosh 7200", CpuFamily::PPC603_604, "pm7200", {}, 0},
        {"pm7500", "Power Macintosh 7500", CpuFamily::PPC603_604, "pm7500", {}, 0},
        {"pm8500", "Power Macintosh 8500", CpuFamily::PPC603_604, "pm8500", {}, 0},
        {"pmg3beige", "Power Macintosh G3 (Beige)", CpuFamily::PPC750, "pmg3beige", {}, 0},
        {"imacg3", "iMac G3 (Rev. A)", CpuFamily::PPC750, "imacg3", {}, 0},
    };
    return catalog;
}

bool machineAvailable(const MachineDesc& m, const std::filesystem::path& romRoot) {
    if (m.phase <= 0) return false; // not yet implemented
    return checkRoms(romRoot, m.romDir, m.roms).available;
}

} // namespace ab
