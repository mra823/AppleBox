// AppleBox — ICpuCore adapter for the vendored MAME 6502-family cores.
// SPDX-License-Identifier: MIT
#pragma once

#include <memory>

#include "core/cpu_core.h"

namespace ab {

// Which member of the family to instantiate. The CMOS part adds the 65C02
// instructions and fixes the NMOS indirect-JMP page-wrap bug, so machines
// must pick deliberately: an Apple II/II+ is NMOS, an enhanced IIe is CMOS.
enum class M6502Variant {
    Nmos6502, // MOS 6502
    W65C02,   // WDC 65C02 (enhanced IIe, IIc)
};

// Cycle-stepped: run(1) advances one clock/bus cycle. setRegister("PC", v)
// re-syncs the prefetch pipeline and performs the opcode fetch bus cycle,
// matching hardware.
class M6502Core final : public ICpuCore {
public:
    explicit M6502Core(M6502Variant variant = M6502Variant::Nmos6502);
    ~M6502Core() override;

    M6502Variant variant() const;

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

    // Cycles executed since power-on, including the slice in progress, so
    // devices reached from a bus access see the correct time.
    Ticks cycles() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ab
