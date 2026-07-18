// AppleBox — Motorola MC6821 PIA.
// SPDX-License-Identifier: MIT
#include "devices/pia6821.h"

#include "core/savestate.h"

namespace ab {

// CRx bits: 7 = IRQx1 flag, 6 = IRQx2 flag (CA2/CB2 input mode, unmodeled),
// 5..3 = CA2/CB2 control, 2 = DDR access, 1 = Cx1 edge select, 0 = Cx1 IRQ
// enable.
namespace {
constexpr u8 kIrq1Flag = 0x80;
constexpr u8 kDdrAccess = 0x04;
constexpr u8 kEdgeSelect = 0x02; // 1 = rising edge, 0 = falling edge
constexpr u8 kIrqEnable = 0x01;
} // namespace

void Pia6821::reset() {
    ora_ = ddra_ = cra_ = 0;
    orb_ = ddrb_ = crb_ = 0;
    ca1_ = cb1_ = false;
    irqA1_ = irqB1_ = false;
    updateIrq();
}

u8 Pia6821::portAPins() const {
    return (ora_ & ddra_) | (portAInput() & ~ddra_);
}

u8 Pia6821::portBPins() const {
    return (orb_ & ddrb_) | (portBInput() & ~ddrb_);
}

u8 Pia6821::read(u8 rs) {
    switch (rs & 3) {
        case 0:
            if (!(cra_ & kDdrAccess)) return ddra_;
            irqA1_ = false; // reading port A clears IRQA flags
            updateIrq();
            return portAPins();
        case 1:
            return cra_ | (irqA1_ ? kIrq1Flag : 0);
        case 2:
            if (!(crb_ & kDdrAccess)) return ddrb_;
            irqB1_ = false;
            updateIrq();
            return portBPins();
        case 3:
        default:
            return crb_ | (irqB1_ ? kIrq1Flag : 0);
    }
}

u8 Pia6821::peek(u8 rs) const {
    switch (rs & 3) {
        case 0: return (cra_ & kDdrAccess) ? portAPins() : ddra_;
        case 1: return cra_ | (irqA1_ ? kIrq1Flag : 0);
        case 2: return (crb_ & kDdrAccess) ? portBPins() : ddrb_;
        case 3:
        default: return crb_ | (irqB1_ ? kIrq1Flag : 0);
    }
}

void Pia6821::write(u8 rs, u8 val) {
    switch (rs & 3) {
        case 0:
            if (cra_ & kDdrAccess) {
                ora_ = val;
                portAOutput((ora_ & ddra_) | (0xff & ~ddra_));
            } else {
                ddra_ = val;
            }
            break;
        case 1:
            cra_ = val & 0x3f; // bits 7:6 are read-only flags
            updateIrq();
            break;
        case 2:
            if (crb_ & kDdrAccess) {
                orb_ = val;
                portBOutput((orb_ & ddrb_) | (0xff & ~ddrb_));
            } else {
                ddrb_ = val;
            }
            break;
        case 3:
            crb_ = val & 0x3f;
            updateIrq();
            break;
    }
}

void Pia6821::setCA1(bool level) {
    bool active = (cra_ & kEdgeSelect) ? (!ca1_ && level) : (ca1_ && !level);
    ca1_ = level;
    if (active) {
        irqA1_ = true;
        updateIrq();
    }
}

void Pia6821::setCB1(bool level) {
    bool active = (crb_ & kEdgeSelect) ? (!cb1_ && level) : (cb1_ && !level);
    cb1_ = level;
    if (active) {
        irqB1_ = true;
        updateIrq();
    }
}

void Pia6821::updateIrq() {
    bool out = (irqA1_ && (cra_ & kIrqEnable)) || (irqB1_ && (crb_ & kIrqEnable));
    if (out != irqOut_) {
        irqOut_ = out;
        irqChanged(out);
    }
}

void Pia6821::serialize(StateVisitor& v) {
    v.value("pia.ora", ora_);
    v.value("pia.ddra", ddra_);
    v.value("pia.cra", cra_);
    v.value("pia.orb", orb_);
    v.value("pia.ddrb", ddrb_);
    v.value("pia.crb", crb_);
    v.value("pia.ca1", ca1_);
    v.value("pia.cb1", cb1_);
    v.value("pia.irqA1", irqA1_);
    v.value("pia.irqB1", irqB1_);
    v.value("pia.irqOut", irqOut_);
}

} // namespace ab
