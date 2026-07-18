// AppleBox — ICpuCore adapter for the vendored MAME m6502 core.
// SPDX-License-Identifier: MIT
#pragma once

#include <memory>

#include "core/cpu_core.h"

namespace ab {

// NMOS 6502 (MAME core behind the scenes). Cycle-stepped: run(1) advances
// one clock/bus cycle. setRegister("PC", v) re-syncs the prefetch pipeline
// and performs the opcode fetch bus cycle, matching hardware.
class M6502Core final : public ICpuCore {
public:
    M6502Core();
    ~M6502Core() override;

    std::string name() const override;
    void attachBus(BusInterface& bus) override;
    void reset() override;
    Ticks run(Ticks cycles) override;
    void abortSlice() override;
    void setIrq(int level, bool asserted) override;
    void setNmi(bool asserted) override;
    std::vector<RegisterInfo> registers() const override;
    u64 getRegister(const std::string& name) const override;
    void setRegister(const std::string& name, u64 value) override;
    u32 pc() const override;
    std::string disassemble(u32 addr, u32& length) override;
    void serialize(StateVisitor& v) override;

    // True when the core sits at an instruction boundary (no partially
    // executed instruction). Used by steppers and the test harness.
    bool atInstructionBoundary() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ab
