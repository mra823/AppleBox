// AppleBox — 5.25" disk images as track bitstreams.
// Every format is normalised to the WOZ model: a quarter-track map into
// variable-length bit streams, which is what the Disk II actually reads.
// Sector images (DSK/DO/PO/2MG) are nibblized on load; WOZ/NIB carry their
// own low-level data.
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/types.h"

namespace ab::media {

// One track's flux transitions as a packed MSB-first bit stream that repeats
// (the drive sees it as an endless loop).
struct Track {
    std::vector<u8> bits;
    u32 bitCount = 0;

    bool empty() const { return bitCount == 0; }
    bool bit(u32 index) const {
        const u32 i = index % bitCount;
        return (bits[i >> 3] >> (7 - (i & 7))) & 1;
    }
};

// Shifts bits out of a track the way the Disk II's read shift register does:
// bits enter from the left and a nibble is complete when the high bit is set.
class TrackReader {
public:
    TrackReader(const Track& track, u32 startBit = 0)
        : track_(track), pos_(track.bitCount ? startBit % track.bitCount : 0) {}

    // Returns the next nibble, or 0 if the track is empty. `bitsConsumed`
    // receives how many bit cells were shifted.
    u8 nextNibble(u32* bitsConsumed = nullptr);

    u32 position() const { return pos_; }

private:
    const Track& track_;
    u32 pos_ = 0;
};

// One decoded 256-byte sector plus its address-field identity.
struct DecodedSector {
    u8 volume = 0;
    u8 track = 0;
    u8 sector = 0;
    std::array<u8, 256> data{};
};

// Scans one revolution of a track and returns every sector it can decode.
std::vector<DecodedSector> decodeTrack(const Track& track);

class DiskImage {
public:
    static constexpr int kQuarterTracks = 160; // 40 tracks × 4
    static constexpr int kSectorsPerTrack = 16;
    static constexpr int kStandardTracks = 35;

    // Loads any supported format by extension and content. On failure returns
    // nullopt and sets `error` when non-null.
    static std::optional<DiskImage> load(const std::filesystem::path& path,
                                         std::string* error = nullptr);

    // Builds a disk from a 143,360-byte sector image. `prodosOrder` selects
    // the ProDOS (.po) interleave instead of the DOS 3.3 (.do/.dsk) one.
    static DiskImage fromSectorImage(std::span<const u8> image,
                                     bool prodosOrder, u8 volume = 254);

    const Track& track(int quarterTrack) const;
    bool writeProtected() const { return writeProtected_; }
    const std::string& format() const { return format_; }
    const std::string& name() const { return name_; }

private:
    std::array<Track, kQuarterTracks> tracks_{};
    bool writeProtected_ = false;
    std::string format_;
    std::string name_;

    static bool loadWoz(std::span<const u8> file, DiskImage& out,
                        std::string* error);
    static bool loadNib(std::span<const u8> file, DiskImage& out,
                        std::string* error);
};

// Nibblizes one track of a sector image (16 × 256 bytes in physical order).
Track buildTrack(std::span<const u8> trackData, u8 trackNum, u8 volume,
                 bool prodosOrder);

} // namespace ab::media
