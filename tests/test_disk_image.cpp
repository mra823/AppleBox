// AppleBox — disk media tests (Phase 2c).
// Round-trips a synthetic 140K disk through GCR nibblization and back, and
// exercises every loader (DO, PO, NIB, 2MG, WOZ2). No Apple ROM or real disk
// image is required, so this always runs in CI.
// SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

#include "media/disk_image.h"
#include "media/gcr6.h"

namespace fs = std::filesystem;
using namespace ab;
using namespace ab::media;

namespace {

int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

// A deterministic 143,360-byte sector image; every sector is identifiable.
std::vector<u8> makeSectorImage() {
    std::vector<u8> img(143360);
    std::mt19937 rng(12345);
    for (int t = 0; t < 35; ++t) {
        for (int s = 0; s < 16; ++s) {
            u8* sec = img.data() + (t * 16 + s) * 256;
            sec[0] = static_cast<u8>(t);
            sec[1] = static_cast<u8>(s);
            for (int i = 2; i < 256; ++i) sec[i] = static_cast<u8>(rng());
        }
    }
    return img;
}

void writeFile(const fs::path& p, std::span<const u8> data) {
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
}

// Verifies a disk decodes back to the original image under `physToLog`.
void checkDisk(const DiskImage& disk, const std::vector<u8>& img,
               bool prodosOrder, const char* label) {
    static constexpr u8 kDos[16] = {0, 13, 11, 9, 7, 5, 3, 1,
                                    14, 12, 10, 8, 6, 4, 2, 15};
    static constexpr u8 kPro[16] = {0, 2, 4, 6, 8, 10, 12, 14,
                                    1, 3, 5, 7, 9, 11, 13, 15};
    const u8* physToLog = prodosOrder ? kPro : kDos;

    int checked = 0;
    for (int t = 0; t < 35; ++t) {
        auto sectors = decodeTrack(disk.track(t * 4));
        if (sectors.size() != 16) {
            std::printf("FAIL %s: track %d decoded %zu sectors, want 16\n",
                        label, t, sectors.size());
            ++failures;
            return;
        }
        for (const auto& s : sectors) {
            CHECK(s.track == t);
            const u8* want = img.data() + (t * 16 + physToLog[s.sector]) * 256;
            if (std::memcmp(s.data.data(), want, 256) != 0) {
                std::printf("FAIL %s: track %d phys sector %d data mismatch\n",
                            label, t, s.sector);
                ++failures;
                return;
            }
            ++checked;
        }
    }
    std::printf("PASS %s (%d sectors round-tripped)\n", label, checked);
}

// Builds a minimal WOZ2 file wrapping the tracks of `src`.
std::vector<u8> makeWoz2(const DiskImage& src) {
    std::vector<u8> out;
    auto push32 = [&](u32 v) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<u8>(v >> (i * 8)));
    };
    auto push16 = [&](u16 v) {
        out.push_back(static_cast<u8>(v));
        out.push_back(static_cast<u8>(v >> 8));
    };

    out.insert(out.end(), {'W', 'O', 'Z', '2', 0xff, 0x0a, 0x0d, 0x0a});
    push32(0); // CRC32 (unchecked by the loader)

    // INFO
    out.insert(out.end(), {'I', 'N', 'F', 'O'});
    push32(60);
    const std::size_t infoStart = out.size();
    out.resize(infoStart + 60, 0);
    out[infoStart + 0] = 2;    // INFO version
    out[infoStart + 1] = 1;    // disk type: 5.25"
    out[infoStart + 2] = 1;    // write protected

    // Collect the distinct tracks (track centres only, as WOZ does).
    std::vector<int> quarterOf;
    for (int t = 0; t < 35; ++t) quarterOf.push_back(t * 4);

    // TMAP: point each track centre and its neighbouring half-tracks at the
    // matching TRKS entry.
    out.insert(out.end(), {'T', 'M', 'A', 'P'});
    push32(160);
    std::vector<u8> tmap(160, 0xff);
    for (std::size_t i = 0; i < quarterOf.size(); ++i) {
        const int q = quarterOf[i];
        tmap[q] = static_cast<u8>(i);
        if (q > 0) tmap[q - 1] = static_cast<u8>(i);
        if (q + 1 < 160) tmap[q + 1] = static_cast<u8>(i);
    }
    out.insert(out.end(), tmap.begin(), tmap.end());

    // TRKS: 160 eight-byte entries, then the bit data on 512-byte blocks.
    out.insert(out.end(), {'T', 'R', 'K', 'S'});
    const std::size_t trksSizeOff = out.size();
    push32(0); // patched below
    const std::size_t trksStart = out.size();

    std::vector<std::vector<u8>> blobs;
    std::vector<u32> bitCounts;
    for (int q : quarterOf) {
        const Track& t = src.track(q);
        blobs.push_back(t.bits);
        bitCounts.push_back(t.bitCount);
    }

    // Bit data begins at the next 512-byte block boundary of the whole file.
    const std::size_t entriesEnd = trksStart + 160 * 8;
    std::size_t block = (entriesEnd + 511) / 512;
    std::vector<std::pair<u32, u32>> placement; // startBlock, blockCount
    for (const auto& b : blobs) {
        const u32 blocks = static_cast<u32>((b.size() + 511) / 512);
        placement.emplace_back(static_cast<u32>(block), blocks);
        block += blocks;
    }
    for (std::size_t i = 0; i < 160; ++i) {
        if (i < placement.size()) {
            push16(static_cast<u16>(placement[i].first));
            push16(static_cast<u16>(placement[i].second));
            push32(bitCounts[i]);
        } else {
            push16(0);
            push16(0);
            push32(0);
        }
    }
    const u32 trksSize = static_cast<u32>(out.size() - trksStart);
    for (int i = 0; i < 4; ++i)
        out[trksSizeOff + i] = static_cast<u8>(trksSize >> (i * 8));

    // Pad to the first data block, then append each track's bits.
    for (std::size_t i = 0; i < blobs.size(); ++i) {
        out.resize(static_cast<std::size_t>(placement[i].first) * 512, 0);
        out.insert(out.end(), blobs[i].begin(), blobs[i].end());
    }
    return out;
}

} // namespace

int main() {
    // --- 6&2 sector codec ---------------------------------------------------
    {
        std::mt19937 rng(7);
        for (int trial = 0; trial < 64; ++trial) {
            std::array<u8, 256> in{}, out{};
            for (u8& b : in) b = static_cast<u8>(rng());
            if (trial == 0) in.fill(0x00);
            if (trial == 1) in.fill(0xff);
            auto nibs = encodeSector62(in);
            CHECK(nibs.size() == kSectorNibbles62);
            // Every nibble must be a legal disk byte.
            for (u8 n : nibs) CHECK((n & 0x80) != 0);
            CHECK(decodeSector62(nibs, out));
            CHECK(std::memcmp(in.data(), out.data(), 256) == 0);
        }
        // A corrupted nibble must fail the checksum.
        std::array<u8, 256> in{}, out{};
        auto nibs = encodeSector62(in);
        nibs[100] = nibs[100] == 0x96 ? 0x97 : 0x96;
        CHECK(!decodeSector62(nibs, out));
        std::printf("PASS 6&2 codec\n");
    }

    // --- 4&4 address-field codec -------------------------------------------
    {
        for (int v = 0; v < 256; ++v) {
            const u8 odd = encode44Odd(static_cast<u8>(v));
            const u8 even = encode44Even(static_cast<u8>(v));
            CHECK((odd & 0x80) != 0 && (even & 0x80) != 0);
            CHECK(decode44(odd, even) == v);
        }
        std::printf("PASS 4&4 codec\n");
    }

    const auto img = makeSectorImage();

    // --- DOS 3.3 and ProDOS interleaves ------------------------------------
    {
        auto dos = DiskImage::fromSectorImage(img, false);
        checkDisk(dos, img, false, "DO interleave");
        auto pro = DiskImage::fromSectorImage(img, true);
        checkDisk(pro, img, true, "PO interleave");
        // The two layouts must actually differ on disk.
        CHECK(dos.track(4).bits != pro.track(4).bits);

        // Track length should be close to a real 300 RPM revolution.
        const u32 bits = dos.track(0).bitCount;
        CHECK(bits > 45000 && bits < 55000);
        std::printf("track bit count: %u\n", bits);

        // Half-track positions read as the adjacent track; quarter tracks
        // between them are unformatted.
        CHECK(dos.track(4 * 5 - 1).bitCount == dos.track(4 * 5).bitCount);
        CHECK(dos.track(4 * 5 + 2).empty());
    }

    // --- File loaders -------------------------------------------------------
    {
        const fs::path dir =
            fs::temp_directory_path() / "applebox_disk_test";
        fs::create_directories(dir);

        writeFile(dir / "test.dsk", img);
        writeFile(dir / "test.po", img);
        std::string err;
        auto dsk = DiskImage::load(dir / "test.dsk", &err);
        CHECK(dsk.has_value());
        if (dsk) checkDisk(*dsk, img, false, "load .dsk");
        auto po = DiskImage::load(dir / "test.po", &err);
        CHECK(po.has_value());
        if (po) checkDisk(*po, img, true, "load .po");

        // 2MG wrapper around the same DOS-order data.
        {
            std::vector<u8> two(64 + img.size(), 0);
            std::memcpy(two.data(), "2IMG", 4);
            two[0x0c] = 0; // DOS 3.3 order
            const u32 dataOff = 64, dataLen = static_cast<u32>(img.size());
            for (int i = 0; i < 4; ++i) {
                two[0x18 + i] = static_cast<u8>(dataOff >> (i * 8));
                two[0x1c + i] = static_cast<u8>(dataLen >> (i * 8));
            }
            std::memcpy(two.data() + 64, img.data(), img.size());
            writeFile(dir / "test.2mg", two);
            auto d = DiskImage::load(dir / "test.2mg", &err);
            CHECK(d.has_value());
            if (d) checkDisk(*d, img, false, "load .2mg");
        }

        // NIB: dump the nibble stream of each DO track.
        if (dsk) {
            std::vector<u8> nib;
            for (int t = 0; t < 35; ++t) {
                TrackReader r(dsk->track(t * 4));
                std::vector<u8> nibs;
                // 6656 nibbles is the classic .nib track length.
                for (int i = 0; i < 6656; ++i) nibs.push_back(r.nextNibble());
                nib.insert(nib.end(), nibs.begin(), nibs.end());
            }
            writeFile(dir / "test.nib", nib);
            auto d = DiskImage::load(dir / "test.nib", &err);
            CHECK(d.has_value());
            if (d) checkDisk(*d, img, false, "load .nib");
        }

        // WOZ2 built from the DO disk's own bit streams.
        if (dsk) {
            auto woz = makeWoz2(*dsk);
            writeFile(dir / "test.woz", woz);
            auto d = DiskImage::load(dir / "test.woz", &err);
            CHECK(d.has_value());
            if (d) {
                CHECK(d->format() == "WOZ2");
                CHECK(d->writeProtected());
                checkDisk(*d, img, false, "load .woz (WOZ2)");
            } else {
                std::printf("  WOZ load error: %s\n", err.c_str());
            }
        }

        fs::remove_all(dir);
    }

    std::printf(failures ? "%d failure(s)\n" : "all disk media tests passed\n",
                failures);
    return failures ? 1 : 0;
}
