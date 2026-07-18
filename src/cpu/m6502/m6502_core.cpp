// AppleBox — ICpuCore adapter for the vendored MAME m6502 core.
// SPDX-License-Identifier: MIT
#include "cpu/m6502/m6502_core.h"

// MAME vendored core: the shim emu.h must precede the vendored headers,
// exactly as MAME's own emu.h does.
#include "emu.h" // src/cpu/mame_shim/emu.h
#include "m6502.h" // src/cpu/m6502/vendor/m6502.h
#include "m6502d.h"

#include <sstream>

#include "core/savestate.h"

namespace ab {

namespace {

// Bridges the MAME memory_interface to AppleBox's BusInterface.
class BusMemoryInterface final : public m6502_device::memory_interface {
public:
    explicit BusMemoryInterface(BusInterface*& bus) : bus_(bus) {}

    u8 read(u16 adr) override { return bus_->read8(adr); }
    u8 read_sync(u16 adr) override { return bus_->read8(adr); }
    u8 read_arg(u16 adr) override { return bus_->read8(adr); }
    void write(u16 adr, u8 val) override { bus_->write8(adr, val); }

private:
    BusInterface*& bus_; // owned by Impl; rebindable via attachBus
};

// data_buffer view over the machine bus for the disassembler.
class BusDataBuffer final : public util::disasm_interface::data_buffer {
public:
    explicit BusDataBuffer(BusInterface* bus) : bus_(bus) {}
    u8 r8(offs_t pc) const override {
        return bus_ ? bus_->read8(static_cast<u32>(pc) & 0xffff) : 0;
    }

private:
    BusInterface* bus_;
};

} // namespace

// Derives from the vendored device to reach its protected register state.
struct M6502Core::Impl : public m6502_device {
    Impl() : m6502_device(g_shim_mconfig, "m6502", nullptr, 1'000'000) {
        set_address_width(16, true); // custom memory interface
        set_custom_memory_interface(std::make_unique<BusMemoryInterface>(bus));
    }

    BusInterface* bus = nullptr;
    bool started = false;

    void start() {
        if (!started) {
            shim_start();
            started = true;
        }
    }

    u64 get(const std::string& n) const {
        if (n == "A") return m_A;
        if (n == "X") return m_X;
        if (n == "Y") return m_Y;
        // Architectural P: bits 5/4 have no storage on the NMOS 6502. By
        // convention bit 5 reads 1 and bit 4 (B) reads 0; B exists only in
        // values pushed by PHP/BRK/IRQ. (MAME keeps both set internally.)
        if (n == "P") return (m_P | F_E) & ~u8(F_B);
        if (n == "SP" || n == "S") return m_SP & 0xff;
        if (n == "PC") return m_NPC;
        if (n == "PC.raw") return m_PC; // internal PC (jammed-CPU testing)
        if (n == "IR") return m_IR;
        return 0;
    }

    void set(const std::string& n, u64 v) {
        if (n == "A") m_A = static_cast<u8>(v);
        else if (n == "X") m_X = static_cast<u8>(v);
        else if (n == "Y") m_Y = static_cast<u8>(v);
        else if (n == "P") {
            m_P = static_cast<u8>(v);
            state_import(device_state_entry(M6502_P)); // forces B|E bits
        } else if (n == "SP" || n == "S") {
            m_SP = 0x0100 | static_cast<u16>(v & 0xff);
        } else if (n == "PC") {
            m_NPC = static_cast<u16>(v);
            // Re-syncs the pipeline: copies NPC->PC and prefetches the
            // opcode (one bus read), as MAME's state interface does.
            state_import(device_state_entry(M6502_PC));
        }
    }

    void serializeState(StateVisitor& v) {
        v.value("m6502.PC", m_PC);
        v.value("m6502.NPC", m_NPC);
        v.value("m6502.PPC", m_PPC);
        v.value("m6502.SP", m_SP);
        v.value("m6502.TMP", m_TMP);
        v.value("m6502.TMP2", m_TMP2);
        v.value("m6502.A", m_A);
        v.value("m6502.X", m_X);
        v.value("m6502.Y", m_Y);
        v.value("m6502.P", m_P);
        v.value("m6502.IR", m_IR);
        v.value("m6502.inst_state", m_inst_state);
        v.value("m6502.inst_substate", m_inst_substate);
        v.value("m6502.inst_state_base", m_inst_state_base);
        v.value("m6502.nmi_state", m_nmi_state);
        v.value("m6502.irq_state", m_irq_state);
        v.value("m6502.nmi_pending", m_nmi_pending);
        v.value("m6502.irq_taken", m_irq_taken);
        v.value("m6502.inhibit_interrupts", m_inhibit_interrupts);
    }

    bool boundary() const { return m_inst_substate == 0; }
};

M6502Core::M6502Core() : impl_(std::make_unique<Impl>()) {}
M6502Core::~M6502Core() = default;

std::string M6502Core::name() const { return "MOS 6502"; }

void M6502Core::attachBus(BusInterface& bus) {
    impl_->bus = &bus;
    impl_->start();
}

void M6502Core::reset() {
    impl_->start();
    impl_->shim_reset();
}

Ticks M6502Core::run(Ticks cycles) {
    return impl_->shim_run(cycles);
}

void M6502Core::abortSlice() { impl_->shim_abort_slice(); }

void M6502Core::setIrq(int level, bool asserted) {
    (void)level; // single IRQ line on the 6502
    impl_->shim_set_input(m6502_device::IRQ_LINE,
                          asserted ? ASSERT_LINE : CLEAR_LINE);
}

void M6502Core::setNmi(bool asserted) {
    impl_->shim_set_input(m6502_device::NMI_LINE,
                          asserted ? ASSERT_LINE : CLEAR_LINE);
}

std::vector<RegisterInfo> M6502Core::registers() const {
    return {{"PC", 16}, {"A", 8},  {"X", 8},  {"Y", 8},
            {"P", 8},   {"SP", 8}, {"IR", 8}};
}

u64 M6502Core::getRegister(const std::string& name) const {
    return impl_->get(name);
}

void M6502Core::setRegister(const std::string& name, u64 value) {
    impl_->set(name, value);
}

u32 M6502Core::pc() const { return impl_->get("PC") & 0xffff; }

std::string M6502Core::disassemble(u32 addr, u32& length) {
    m6502_disassembler dasm;
    BusDataBuffer buf(impl_->bus);
    std::ostringstream os;
    offs_t r = dasm.disassemble(os, addr & 0xffff, buf, buf);
    length = r & util::disasm_interface::LENGTHMASK;
    return os.str();
}

void M6502Core::serialize(StateVisitor& v) { impl_->serializeState(v); }

bool M6502Core::atInstructionBoundary() const { return impl_->boundary(); }

} // namespace ab
