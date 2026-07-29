// AppleBox — ICpuCore adapter for the vendored MAME 6502-family cores.
// SPDX-License-Identifier: MIT
#include "cpu/m6502/m6502_core.h"

// MAME vendored core: the shim emu.h must precede the vendored headers,
// exactly as MAME's own emu.h does.
#include "emu.h" // src/cpu/mame_shim/emu.h
#include "m6502.h" // src/cpu/m6502/vendor/m6502.h
#include "m6502d.h"
#include "w65c02.h"
#include "w65c02d.h"

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

// The register file and pipeline state live in m6502_device, so every
// variant shares one accessor; only the concrete device type differs.
// This interface lets Impl hold any of them behind one pointer.
class DeviceAccess {
public:
    virtual ~DeviceAccess() = default;
    virtual m6502_device& dev() = 0;
    virtual const m6502_device& dev() const = 0;
    virtual u64 get(const std::string& n) const = 0;
    virtual void set(const std::string& n, u64 v) = 0;
    virtual void serializeState(StateVisitor& v) = 0;
    virtual bool boundary() const = 0;
    // create_disassembler() is protected on the device, so each variant
    // hands out its own disassembler from inside the class.
    virtual std::unique_ptr<util::disasm_interface> makeDisassembler() = 0;
};

// Derives from the vendored device to reach its protected register state.
template <typename Device>
class DeviceImpl final : public Device, public DeviceAccess {
public:
    DeviceImpl(const char* tag, BusInterface*& bus)
        : Device(g_shim_mconfig, tag, nullptr, 1'000'000) {
        this->set_address_width(16, true); // custom memory interface
        this->set_custom_memory_interface(
            std::make_unique<BusMemoryInterface>(bus));
    }

    m6502_device& dev() override { return *this; }
    const m6502_device& dev() const override { return *this; }

    u64 get(const std::string& n) const override {
        if (n == "A") return this->m_A;
        if (n == "X") return this->m_X;
        if (n == "Y") return this->m_Y;
        // Architectural P: bits 5/4 have no storage on the 6502. By
        // convention bit 5 reads 1 and bit 4 (B) reads 0; B exists only in
        // values pushed by PHP/BRK/IRQ. (MAME keeps both set internally.)
        if (n == "P") return (this->m_P | Device::F_E) & ~u8(Device::F_B);
        if (n == "SP" || n == "S") return this->m_SP & 0xff;
        if (n == "PC") return this->m_NPC;
        if (n == "PC.raw") return this->m_PC; // internal PC (jammed CPU)
        if (n == "IR") return this->m_IR;
        return 0;
    }

    void set(const std::string& n, u64 v) override {
        if (n == "A") this->m_A = static_cast<u8>(v);
        else if (n == "X") this->m_X = static_cast<u8>(v);
        else if (n == "Y") this->m_Y = static_cast<u8>(v);
        else if (n == "P") {
            this->m_P = static_cast<u8>(v);
            this->state_import(device_state_entry(M6502_P)); // forces B|E
        } else if (n == "SP" || n == "S") {
            this->m_SP = 0x0100 | static_cast<u16>(v & 0xff);
        } else if (n == "PC") {
            this->m_NPC = static_cast<u16>(v);
            // Re-syncs the pipeline: copies NPC->PC and prefetches the
            // opcode (one bus read), as MAME's state interface does.
            this->state_import(device_state_entry(M6502_PC));
        }
    }

    void serializeState(StateVisitor& v) override {
        v.value("m6502.PC", this->m_PC);
        v.value("m6502.NPC", this->m_NPC);
        v.value("m6502.PPC", this->m_PPC);
        v.value("m6502.SP", this->m_SP);
        v.value("m6502.TMP", this->m_TMP);
        v.value("m6502.TMP2", this->m_TMP2);
        v.value("m6502.A", this->m_A);
        v.value("m6502.X", this->m_X);
        v.value("m6502.Y", this->m_Y);
        v.value("m6502.P", this->m_P);
        v.value("m6502.IR", this->m_IR);
        v.value("m6502.inst_state", this->m_inst_state);
        v.value("m6502.inst_substate", this->m_inst_substate);
        v.value("m6502.inst_state_base", this->m_inst_state_base);
        v.value("m6502.nmi_state", this->m_nmi_state);
        v.value("m6502.irq_state", this->m_irq_state);
        v.value("m6502.nmi_pending", this->m_nmi_pending);
        v.value("m6502.irq_taken", this->m_irq_taken);
        v.value("m6502.inhibit_interrupts", this->m_inhibit_interrupts);
    }

    bool boundary() const override { return this->m_inst_substate == 0; }

    std::unique_ptr<util::disasm_interface> makeDisassembler() override {
        return this->create_disassembler();
    }
};

} // namespace

struct M6502Core::Impl {
    Impl(M6502Variant v) : variant(v) {
        if (v == M6502Variant::W65C02)
            access = std::make_unique<DeviceImpl<w65c02_device>>("w65c02", bus);
        else
            access = std::make_unique<DeviceImpl<m6502_device>>("m6502", bus);
    }

    M6502Variant variant;
    BusInterface* bus = nullptr;
    std::unique_ptr<DeviceAccess> access;
    bool started = false;

    m6502_device& dev() { return access->dev(); }

    void start() {
        if (!started) {
            dev().shim_start();
            started = true;
        }
    }
};

M6502Core::M6502Core(M6502Variant variant)
    : impl_(std::make_unique<Impl>(variant)) {}
M6502Core::~M6502Core() = default;

M6502Variant M6502Core::variant() const { return impl_->variant; }

std::string M6502Core::name() const {
    return impl_->variant == M6502Variant::W65C02 ? "WDC 65C02" : "MOS 6502";
}

void M6502Core::attachBus(BusInterface& bus) {
    impl_->bus = &bus;
    impl_->start();
}

void M6502Core::reset() {
    impl_->start();
    impl_->dev().shim_reset();
}

Ticks M6502Core::run(Ticks cycles) { return impl_->dev().shim_run(cycles); }

void M6502Core::abortSlice() { impl_->dev().shim_abort_slice(); }

void M6502Core::setIrq(int level, bool asserted) {
    (void)level; // single IRQ line on the 6502
    impl_->dev().shim_set_input(m6502_device::IRQ_LINE,
                                asserted ? ASSERT_LINE : CLEAR_LINE);
}

void M6502Core::setNmi(bool asserted) {
    impl_->dev().shim_set_input(m6502_device::NMI_LINE,
                                asserted ? ASSERT_LINE : CLEAR_LINE);
}

std::vector<RegisterInfo> M6502Core::registers() const {
    return {{"PC", 16}, {"A", 8},  {"X", 8},  {"Y", 8},
            {"P", 8},   {"SP", 8}, {"IR", 8}};
}

u64 M6502Core::getRegister(const std::string& name) const {
    return impl_->access->get(name);
}

void M6502Core::setRegister(const std::string& name, u64 value) {
    impl_->access->set(name, value);
}

u32 M6502Core::pc() const { return impl_->access->get("PC") & 0xffff; }

std::string M6502Core::disassemble(u32 addr, u32& length) {
    auto dasm = impl_->access->makeDisassembler();
    BusDataBuffer buf(impl_->bus);
    std::ostringstream os;
    offs_t r = dasm->disassemble(os, addr & 0xffff, buf, buf);
    length = r & util::disasm_interface::LENGTHMASK;
    return os.str();
}

void M6502Core::serialize(StateVisitor& v) { impl_->access->serializeState(v); }

bool M6502Core::atInstructionBoundary() const { return impl_->access->boundary(); }

Ticks M6502Core::cycles() const { return impl_->dev().shim_cycles_now(); }

} // namespace ab
