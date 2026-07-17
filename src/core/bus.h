// AppleBox — abstract bus interface presented to CPU cores.
// SPDX-License-Identifier: MIT
#pragma once

#include "core/types.h"

namespace ab {

// Address-space / function-code hint. Maps to 68k function codes and to
// separate program/data spaces on CPUs that distinguish them; FLAT for 6502
// family.
enum class AddrSpace : u8 {
    Flat,
    UserData,
    UserProgram,
    SupervisorData,
    SupervisorProgram,
    Cpu, // 68k CPU space (interrupt acknowledge, coprocessor)
};

// Implemented by each machine; consumed by CPU core adapters. Every access is
// a bus cycle — implementations may consume scheduler time for contention.
class BusInterface {
public:
    virtual ~BusInterface() = default;

    virtual u8 read8(u32 addr, AddrSpace sp = AddrSpace::Flat) = 0;
    virtual void write8(u32 addr, u8 val, AddrSpace sp = AddrSpace::Flat) = 0;

    // Default implementations compose 8-bit accesses big-endian (6502 adapters
    // never call these; 68k/PPC machines override with native bus behavior).
    virtual u16 read16(u32 addr, AddrSpace sp = AddrSpace::Flat) {
        return static_cast<u16>(read8(addr, sp) << 8 | read8(addr + 1, sp));
    }
    virtual void write16(u32 addr, u16 val, AddrSpace sp = AddrSpace::Flat) {
        write8(addr, static_cast<u8>(val >> 8), sp);
        write8(addr + 1, static_cast<u8>(val), sp);
    }
    virtual u32 read32(u32 addr, AddrSpace sp = AddrSpace::Flat) {
        return static_cast<u32>(read16(addr, sp)) << 16 | read16(addr + 2, sp);
    }
    virtual void write32(u32 addr, u32 val, AddrSpace sp = AddrSpace::Flat) {
        write16(addr, static_cast<u16>(val >> 16), sp);
        write16(addr + 2, static_cast<u16>(val), sp);
    }
};

} // namespace ab
