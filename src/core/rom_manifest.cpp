// SPDX-License-Identifier: MIT
#include "core/rom_manifest.h"

#include <fstream>

#include "core/sha256.h"

namespace ab {
namespace {

std::optional<std::vector<u8>> readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<u8> data((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    return data;
}

bool validate(const std::vector<u8>& data, const RomFile& file) {
    if (data.size() != file.size) return false;
    if (!file.sha256.empty() && sha256Hex(data) != file.sha256) return false;
    return true;
}

} // namespace

RomCheckResult checkRoms(const std::filesystem::path& romRoot,
                         const std::string& machineDir,
                         const std::vector<RomFile>& files) {
    RomCheckResult result;
    result.available = true;
    for (const auto& f : files) {
        auto data = readFile(romRoot / machineDir / f.filename);
        if (!data) {
            if (!f.optional) {
                result.missing.push_back(f.filename);
                result.available = false;
            }
            continue;
        }
        if (!validate(*data, f)) {
            result.badHash.push_back(f.filename);
            if (!f.optional) result.available = false;
        }
    }
    return result;
}

std::optional<std::vector<u8>> loadRom(const std::filesystem::path& romRoot,
                                       const std::string& machineDir,
                                       const RomFile& file) {
    auto data = readFile(romRoot / machineDir / file.filename);
    if (!data || !validate(*data, file)) return std::nullopt;
    return data;
}

} // namespace ab
