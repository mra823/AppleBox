// AppleBox — ROM manifest / sha256 tests.
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "core/machine.h"
#include "core/rom_manifest.h"
#include "core/sha256.h"

using namespace ab;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

int main() {
    // SHA-256 known-answer tests (FIPS 180-4 examples).
    {
        CHECK(sha256Hex({}) ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        const char* abc = "abc";
        CHECK(sha256Hex(std::span<const u8>(reinterpret_cast<const u8*>(abc), 3)) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        // 56-byte message exercises the two-block padding path.
        const char* m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        CHECK(sha256Hex(std::span<const u8>(reinterpret_cast<const u8*>(m), 56)) ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    }

    // ROM gate: missing → unavailable; correct file → available; bad hash → rejected.
    {
        fs::path root = fs::temp_directory_path() / "applebox_test_roms";
        fs::remove_all(root);
        fs::create_directories(root / "testmach");

        RomFile rom{"test.rom", 3,
                    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                    false};

        auto r1 = checkRoms(root, "testmach", {rom});
        CHECK(!r1.available && r1.missing.size() == 1);

        std::ofstream(root / "testmach" / "test.rom", std::ios::binary) << "abc";
        auto r2 = checkRoms(root, "testmach", {rom});
        CHECK(r2.available && r2.missing.empty() && r2.badHash.empty());
        auto data = loadRom(root, "testmach", rom);
        CHECK(data && data->size() == 3);

        std::ofstream(root / "testmach" / "test.rom", std::ios::binary) << "xyz";
        auto r3 = checkRoms(root, "testmach", {rom});
        CHECK(!r3.available && r3.badHash.size() == 1);
        CHECK(!loadRom(root, "testmach", rom));

        // Optional ROM missing does not gate availability.
        RomFile opt{"basic.rom", 3, "", true};
        auto r4 = checkRoms(root, "testmach", {opt});
        CHECK(r4.available);

        fs::remove_all(root);
    }

    // Machine catalog sanity: Phase 0 → nothing available yet.
    {
        CHECK(machineCatalog().size() >= 30);
        for (const auto& m : machineCatalog())
            CHECK(!machineAvailable(m, "roms"));
    }

    std::printf(failures ? "%d failure(s)\n" : "all rom_manifest tests passed\n", failures);
    return failures;
}
