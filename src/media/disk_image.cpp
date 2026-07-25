// AppleBox — 5.25" disk image loading and nibblization.
// SPDX-License-Identifier: MIT
#include "media/disk_image.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "media/gcr6.h"

namespace ab::media {

namespace {

constexpr std::size_t kSectorImageSize = 143360; // 35 × 16 × 256
constexpr std::size_t kNibTrackSize = 6656;
constexpr std::size_t kNibImageSize = kNibTrackSize * 35;

// Physical sector -> logical (image) sector. DOS 3.3 uses a 2:1 soft skew;
// ProDOS images are stored in block order.
constexpr u8 kDosPhysToLog[16] = {0, 13, 11, 9, 7, 5, 3, 1,
                                  14, 12, 10, 8, 6, 4, 2, 15};
constexpr u8 kProdosPhysToLog[16] = {0, 2, 4, 6, 8, 10, 12, 14,
                                     1, 3, 5, 7, 9, 11, 13, 15};

// Track layout: a 300 RPM disk at 4 µs per bit cell holds ~50,000 bits.
constexpr int kGap1SyncBytes = 64;
constexpr int kGap2SyncBytes = 6;
constexpr int kGap3SyncBytes = 12;

class BitWriter {
public:
    void writeBits(u32 value, int count) {
        for (int i = count - 1; i >= 0; --i) push((value >> i) & 1);
    }
    void writeNibble(u8 n) { writeBits(n, 8); }
    // A self-sync byte is $FF held for ten bit cells, so the reader's shift
    // register realigns to the byte boundary.
    void writeSync(int count) {
        for (int i = 0; i < count; ++i) writeBits(0xff << 2, 10);
    }
    Track take() {
        Track t;
        t.bits = std::move(bits_);
        t.bitCount = count_;
        return t;
    }

private:
    void push(u32 bit) {
        if ((count_ & 7) == 0) bits_.push_back(0);
        if (bit) bits_.back() |= static_cast<u8>(0x80 >> (count_ & 7));
        ++count_;
    }
    std::vector<u8> bits_;
    u32 count_ = 0;
};

void writeAddressField(BitWriter& w, u8 volume, u8 track, u8 sector) {
    w.writeNibble(0xd5);
    w.writeNibble(0xaa);
    w.writeNibble(0x96);
    for (u8 v : {volume, track, sector,
                 static_cast<u8>(volume ^ track ^ sector)}) {
        w.writeNibble(encode44Odd(v));
        w.writeNibble(encode44Even(v));
    }
    w.writeNibble(0xde);
    w.writeNibble(0xaa);
    w.writeNibble(0xeb);
}

void writeDataField(BitWriter& w, std::span<const u8> sector) {
    w.writeNibble(0xd5);
    w.writeNibble(0xaa);
    w.writeNibble(0xad);
    for (u8 n : encodeSector62(sector)) w.writeNibble(n);
    w.writeNibble(0xde);
    w.writeNibble(0xaa);
    w.writeNibble(0xeb);
}

u16 rd16(std::span<const u8> d, std::size_t off) {
    return static_cast<u16>(d[off] | (d[off + 1] << 8));
}
u32 rd32(std::span<const u8> d, std::size_t off) {
    return static_cast<u32>(d[off]) | (static_cast<u32>(d[off + 1]) << 8) |
           (static_cast<u32>(d[off + 2]) << 16) |
           (static_cast<u32>(d[off + 3]) << 24);
}

} // namespace

u8 TrackReader::nextNibble(u32* bitsConsumed) {
    if (track_.empty()) {
        if (bitsConsumed) *bitsConsumed = 0;
        return 0;
    }
    u8 shifter = 0;
    u32 used = 0;
    // Bound the search at two revolutions: a track of all zero bits (an
    // unformatted track) would otherwise spin forever.
    const u32 limit = track_.bitCount * 2 + 16;
    while (used < limit) {
        shifter = static_cast<u8>((shifter << 1) | (track_.bit(pos_) ? 1 : 0));
        pos_ = (pos_ + 1) % track_.bitCount;
        ++used;
        if (shifter & 0x80) break;
    }
    if (bitsConsumed) *bitsConsumed = used;
    return shifter;
}

std::vector<DecodedSector> decodeTrack(const Track& track) {
    std::vector<DecodedSector> out;
    if (track.empty()) return out;

    TrackReader reader(track);
    // Scan a little over one revolution so a sector straddling the start
    // position is still seen; duplicates are dropped below.
    const u32 budget = track.bitCount + track.bitCount / 8;
    u32 consumed = 0;
    u8 win[3] = {0, 0, 0};
    DecodedSector pending;
    bool havePending = false;
    bool seen[256] = {};

    while (consumed < budget) {
        u32 used = 0;
        const u8 n = reader.nextNibble(&used);
        if (used == 0) break;
        consumed += used;
        win[0] = win[1];
        win[1] = win[2];
        win[2] = n;

        if (win[0] == 0xd5 && win[1] == 0xaa && win[2] == 0x96) {
            u8 f[8];
            for (u8& v : f) {
                v = reader.nextNibble(&used);
                consumed += used;
            }
            pending.volume = decode44(f[0], f[1]);
            pending.track = decode44(f[2], f[3]);
            pending.sector = decode44(f[4], f[5]);
            const u8 sum = decode44(f[6], f[7]);
            havePending = sum == (pending.volume ^ pending.track ^
                                  pending.sector);
            win[0] = win[1] = win[2] = 0;
        } else if (win[0] == 0xd5 && win[1] == 0xaa && win[2] == 0xad &&
                   havePending) {
            std::vector<u8> nibs(kSectorNibbles62);
            for (u8& v : nibs) {
                v = reader.nextNibble(&used);
                consumed += used;
            }
            if (decodeSector62(nibs, pending.data) && !seen[pending.sector]) {
                seen[pending.sector] = true;
                out.push_back(pending);
            }
            havePending = false;
            win[0] = win[1] = win[2] = 0;
        }
    }
    return out;
}

Track buildTrack(std::span<const u8> trackData, u8 trackNum, u8 volume,
                 bool prodosOrder) {
    const u8* physToLog = prodosOrder ? kProdosPhysToLog : kDosPhysToLog;
    BitWriter w;
    w.writeSync(kGap1SyncBytes);
    for (int phys = 0; phys < DiskImage::kSectorsPerTrack; ++phys) {
        const int logical = physToLog[phys];
        writeAddressField(w, volume, trackNum, static_cast<u8>(phys));
        w.writeSync(kGap2SyncBytes);
        writeDataField(w, trackData.subspan(logical * 256, 256));
        w.writeSync(kGap3SyncBytes);
    }
    return w.take();
}

DiskImage DiskImage::fromSectorImage(std::span<const u8> image,
                                     bool prodosOrder, u8 volume) {
    DiskImage disk;
    disk.format_ = prodosOrder ? "PO" : "DO";
    const int trackCount =
        static_cast<int>(std::min<std::size_t>(image.size() / (16 * 256), 40));
    for (int t = 0; t < trackCount; ++t) {
        Track built = buildTrack(image.subspan(t * 16 * 256, 16 * 256),
                                 static_cast<u8>(t), volume, prodosOrder);
        // A standard disk is readable at the track centre and at the
        // half-track either side, as WOZ's TMAP also describes.
        const int q = t * 4;
        if (q > 0) disk.tracks_[q - 1] = built;
        if (q + 1 < kQuarterTracks) disk.tracks_[q + 1] = built;
        disk.tracks_[q] = std::move(built);
    }
    return disk;
}

bool DiskImage::loadNib(std::span<const u8> file, DiskImage& out,
                        std::string* error) {
    if (file.size() < kNibTrackSize) {
        if (error) *error = "NIB image too small";
        return false;
    }
    out.format_ = "NIB";
    const int trackCount =
        static_cast<int>(std::min<std::size_t>(file.size() / kNibTrackSize, 40));
    for (int t = 0; t < trackCount; ++t) {
        BitWriter w;
        for (std::size_t i = 0; i < kNibTrackSize; ++i)
            w.writeNibble(file[t * kNibTrackSize + i]);
        Track built = w.take();
        const int q = t * 4;
        if (q > 0) out.tracks_[q - 1] = built;
        if (q + 1 < kQuarterTracks) out.tracks_[q + 1] = built;
        out.tracks_[q] = std::move(built);
    }
    return true;
}

bool DiskImage::loadWoz(std::span<const u8> file, DiskImage& out,
                        std::string* error) {
    if (file.size() < 12) {
        if (error) *error = "WOZ file too small";
        return false;
    }
    const bool woz2 = std::memcmp(file.data(), "WOZ2", 4) == 0;
    const bool woz1 = std::memcmp(file.data(), "WOZ1", 4) == 0;
    if (!woz1 && !woz2) {
        if (error) *error = "not a WOZ file";
        return false;
    }
    out.format_ = woz2 ? "WOZ2" : "WOZ1";

    std::span<const u8> tmap, trks;
    std::size_t off = 12;
    while (off + 8 <= file.size()) {
        const std::size_t size = rd32(file, off + 4);
        if (off + 8 + size > file.size()) break;
        auto chunk = file.subspan(off + 8, size);
        if (std::memcmp(file.data() + off, "INFO", 4) == 0) {
            if (size >= 2) out.writeProtected_ = chunk[1] != 0;
        } else if (std::memcmp(file.data() + off, "TMAP", 4) == 0) {
            tmap = chunk;
        } else if (std::memcmp(file.data() + off, "TRKS", 4) == 0) {
            trks = chunk;
        }
        off += 8 + size;
    }
    if (tmap.size() < kQuarterTracks || trks.empty()) {
        if (error) *error = "WOZ missing TMAP or TRKS chunk";
        return false;
    }

    for (int q = 0; q < kQuarterTracks; ++q) {
        const u8 idx = tmap[q];
        if (idx == 0xff) continue;
        Track t;
        if (woz2) {
            const std::size_t e = idx * 8u;
            if (e + 8 > trks.size()) continue;
            const u32 startBlock = rd16(trks, e);
            const u32 blockCount = rd16(trks, e + 2);
            const u32 bitCount = rd32(trks, e + 4);
            const std::size_t byteOff = static_cast<std::size_t>(startBlock) * 512;
            const std::size_t byteLen =
                std::min<std::size_t>(blockCount * 512u, (bitCount + 7) / 8);
            if (bitCount == 0 || byteOff + byteLen > file.size()) continue;
            t.bits.assign(file.begin() + byteOff,
                          file.begin() + byteOff + byteLen);
            t.bitCount = bitCount;
        } else {
            // WOZ1: fixed 6656-byte records, trailing bytesUsed/bitCount.
            const std::size_t rec = idx * 6656u;
            if (rec + 6656 > trks.size()) continue;
            const u32 bitCount = rd16(trks, rec + 6648 + 2);
            if (bitCount == 0) continue;
            const std::size_t byteLen =
                std::min<std::size_t>(6646, (bitCount + 7) / 8);
            t.bits.assign(trks.begin() + rec, trks.begin() + rec + byteLen);
            t.bitCount = bitCount;
        }
        out.tracks_[q] = std::move(t);
    }
    return true;
}

std::optional<DiskImage> DiskImage::load(const std::filesystem::path& path,
                                         std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path.string();
        return std::nullopt;
    }
    std::vector<u8> file((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    if (file.empty()) {
        if (error) *error = "empty file";
        return std::nullopt;
    }

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    DiskImage disk;
    disk.name_ = path.filename().string();

    if (file.size() >= 4 && std::memcmp(file.data(), "WOZ", 3) == 0) {
        if (!loadWoz(file, disk, error)) return std::nullopt;
        return disk;
    }

    std::span<const u8> body = file;
    bool prodosOrder = ext == ".po";
    if (ext == ".2mg" || (file.size() > 64 &&
                          std::memcmp(file.data(), "2IMG", 4) == 0)) {
        // 2MG: 64-byte header; format 0 = DOS order, 1 = ProDOS order.
        if (file.size() < 64) {
            if (error) *error = "2MG header truncated";
            return std::nullopt;
        }
        const u32 imageFormat = rd32(file, 0x0c);
        const u32 dataOff = rd32(file, 0x18);
        const u32 dataLen = rd32(file, 0x1c);
        if (dataOff + dataLen > file.size()) {
            if (error) *error = "2MG data block out of range";
            return std::nullopt;
        }
        const u32 flags = rd32(file, 0x10);
        disk.writeProtected_ = (flags & 0x80000000u) != 0;
        body = std::span<const u8>(file).subspan(dataOff, dataLen);
        if (imageFormat == 2) { // nibble image
            if (!loadNib(body, disk, error)) return std::nullopt;
            disk.format_ = "2MG/NIB";
            return disk;
        }
        prodosOrder = imageFormat == 1;
        DiskImage built = fromSectorImage(body, prodosOrder);
        built.name_ = disk.name_;
        built.writeProtected_ = disk.writeProtected_;
        built.format_ = prodosOrder ? "2MG/PO" : "2MG/DO";
        return built;
    }

    if (ext == ".nib" || body.size() == kNibImageSize) {
        if (!loadNib(body, disk, error)) return std::nullopt;
        return disk;
    }

    if (body.size() < kSectorImageSize) {
        if (error) *error = "unrecognised or truncated disk image";
        return std::nullopt;
    }
    DiskImage built = fromSectorImage(body, prodosOrder);
    built.name_ = disk.name_;
    return built;
}

const Track& DiskImage::track(int quarterTrack) const {
    static const Track kEmpty{};
    if (quarterTrack < 0 || quarterTrack >= kQuarterTracks) return kEmpty;
    return tracks_[quarterTrack];
}

} // namespace ab::media
