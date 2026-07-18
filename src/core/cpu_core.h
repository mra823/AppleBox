// AppleBox — abstract CPU core interface. Heterogeneous vendored cores
// (MAME 6502/65816/PPC, Moira, Musashi) plug in behind this via adapters.
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

#include "core/bus.h"
#include "core/types.h"

namespace ab {

class StateVisitor;

struct RegisterInfo {
    std::string name;
    u32 bits = 8; // register width in bits
};

class ICpuCore {
public:
    virtual ~ICpuCore() = default;

    virtual std::string name() const = 0;

    // Attach the machine bus before first reset.
    virtual void attachBus(BusInterface& bus) = 0;

    virtual void reset() = 0;

    // Run for at least `cycles` CPU clock cycles; returns cycles actually
    // consumed (a core may overshoot to finish an instruction).
    virtual Ticks run(Ticks cycles) = 0;

    // Abort the current run() slice at the next resumable point. Devices
    // call this when a bus access schedules an event earlier than the
    // slice's end, so the machine loop can recompute its slice bound.
    virtual void abortSlice() {}

    // Interrupt lines.
    virtual void setIrq(int level, bool asserted) = 0;
    virtual void setNmi(bool asserted) = 0;

    // Register access for the debugger.
    virtual std::vector<RegisterInfo> registers() const = 0;
    virtual u64 getRegister(const std::string& name) const = 0;
    virtual void setRegister(const std::string& name, u64 value) = 0;
    virtual u32 pc() const = 0;

    // Disassemble one instruction at addr; returns text, sets `length` to the
    // instruction byte length.
    virtual std::string disassemble(u32 addr, u32& length) = 0;

    // Versioned save-state.
    virtual void serialize(StateVisitor& v) = 0;
};

} // namespace ab
