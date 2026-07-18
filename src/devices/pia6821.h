// AppleBox — Motorola MC6821 PIA (Peripheral Interface Adapter).
// Original implementation from the Motorola MC6821 datasheet.
// SPDX-License-Identifier: MIT
#pragma once

#include <functional>

#include "core/device.h"
#include "core/types.h"

namespace ab {

class StateVisitor;

// Register map (RS1,RS0): 0 = ORA/DDRA, 1 = CRA, 2 = ORB/DDRB, 3 = CRB.
// Control register bit 2 selects DDR (0) or output/peripheral register (1).
class Pia6821 final : public Device {
public:
    Pia6821() : Device("pia6821") {}

    // Peripheral wiring. inputA/inputB supply the port pins that are
    // configured as inputs; outputB/outputA observe writes to the output
    // registers. irq drives the CPU interrupt line (level = IRQA|IRQB).
    std::function<u8()> portAInput = [] { return 0xff; };
    std::function<u8()> portBInput = [] { return 0xff; };
    std::function<void(u8)> portAOutput = [](u8) {};
    std::function<void(u8)> portBOutput = [](u8) {};
    std::function<void(bool)> irqChanged = [](bool) {};

    void reset() override;

    u8 read(u8 rs);          // rs = 0..3
    void write(u8 rs, u8 val);
    u8 peek(u8 rs) const;    // side-effect-free (debugger)

    // Control line transitions (keyboard strobe etc.).
    void setCA1(bool level);
    void setCB1(bool level);

    // Effective pin values on each port (inputs merged with driven outputs).
    u8 portAPins() const;
    u8 portBPins() const;

    void serialize(StateVisitor& v) override;

private:
    void updateIrq();

    u8 ora_ = 0, ddra_ = 0, cra_ = 0;
    u8 orb_ = 0, ddrb_ = 0, crb_ = 0;
    bool ca1_ = false, cb1_ = false;
    bool irqA1_ = false, irqB1_ = false; // CRx bit 7 flags
    bool irqOut_ = false;
};

} // namespace ab
