// AppleBox — minimal MAME infrastructure shim ("emu.h").
//
// Vendored MAME CPU cores (src/cpu/*/vendor/) are compiled verbatim against
// this header instead of real MAME. It fakes just enough of MAME's device
// model for a bare CPU core driven through set_custom_memory_interface():
// device construction, input lines, the execute interface, and the
// disassembler interface. State registration, save-state registration and
// debugger hooks are stubs — AppleBox handles those in its adapters.
//
// This file is original AppleBox code (MIT). It contains no MAME code.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Fundamental types
// ---------------------------------------------------------------------------
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;
using offs_t = u32;

#define ATTR_COLD

// ---------------------------------------------------------------------------
// Line states / input lines
// ---------------------------------------------------------------------------
enum line_state {
    CLEAR_LINE = 0,
    ASSERT_LINE,
    HOLD_LINE,
};

enum {
    INPUT_LINE_IRQ0 = 0,
    INPUT_LINE_IRQ1 = 1,
    INPUT_LINE_IRQ2 = 2,
    INPUT_LINE_IRQ3 = 3,
    INPUT_LINE_NMI = 32,
    INPUT_LINE_RESET = 33,
    INPUT_LINE_HALT = 34,
};

enum { ENDIANNESS_LITTLE = 0, ENDIANNESS_BIG = 1 };

// Address space indices.
enum { AS_PROGRAM = 0, AS_DATA = 1, AS_IO = 2, AS_OPCODES = 3 };

// ---------------------------------------------------------------------------
// util:: pieces used by core + disassembler code
// ---------------------------------------------------------------------------
namespace util {

namespace detail {
// printf-style formatting into a std::string. MAME's stream_format uses
// %-style formats in the cores we vendor; vsnprintf covers them.
inline std::string vformat(const char* fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = std::vsnprintf(nullptr, 0, fmt, ap2);
    va_end(ap2);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    std::vsnprintf(out.data(), out.size() + 1, fmt, ap);
    return out;
}
inline std::string format(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string out = vformat(fmt, ap);
    va_end(ap);
    return out;
}
} // namespace detail

template <typename... Args>
void stream_format(std::ostream& os, const char* fmt, Args&&... args) {
    os << detail::format(fmt, std::forward<Args>(args)...);
}

// Disassembler interface (subset of MAME's util::disasm_interface).
class disasm_interface {
public:
    // Flag constants: low bits of the return value are the instruction
    // length; these OR in metadata.
    static constexpr u32 STEP_OVER = 0x10000000;
    static constexpr u32 STEP_OUT = 0x20000000;
    static constexpr u32 STEP_COND = 0x40000000;
    static constexpr u32 SUPPORTED = 0x80000000;
    static constexpr u32 LENGTHMASK = 0x0000ffff;

    class data_buffer {
    public:
        virtual ~data_buffer() = default;
        virtual u8 r8(offs_t pc) const = 0;
    };

    virtual ~disasm_interface() = default;
    virtual u32 opcode_alignment() const = 0;
    virtual offs_t disassemble(std::ostream& stream, offs_t pc,
                               const data_buffer& opcodes,
                               const data_buffer& params) = 0;
};

} // namespace util

// The vendored disassembler sources reference these unqualified (they are
// members of util::disasm_interface in MAME, found via inheritance) — and
// also as bare enum-like constants in generated tables.
using data_buffer = util::disasm_interface::data_buffer;
constexpr u32 STEP_OVER = util::disasm_interface::STEP_OVER;
constexpr u32 STEP_OUT = util::disasm_interface::STEP_OUT;
constexpr u32 STEP_COND = util::disasm_interface::STEP_COND;
constexpr u32 SUPPORTED = util::disasm_interface::SUPPORTED;

// string_format used by state_string_export.
template <typename... Args>
std::string string_format(const char* fmt, Args&&... args) {
    return util::detail::format(fmt, std::forward<Args>(args)...);
}

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
class device_t;
class machine_config;
class device_state_entry;

// device_type: in MAME this is a rich factory object; here it is only an
// identity token used by DEFINE/DECLARE_DEVICE_TYPE.
struct device_type_token {
    const char* shortname = nullptr;
    const char* fullname = nullptr;
};
using device_type = const device_type_token&;

#define DECLARE_DEVICE_TYPE(Type, Class) extern const device_type_token Type;
#define DEFINE_DEVICE_TYPE(Type, Class, ShortName, FullName) \
    extern const device_type_token Type{ShortName, FullName};

// ---------------------------------------------------------------------------
// machine_config — opaque placeholder passed through constructors.
// ---------------------------------------------------------------------------
class machine_config {
public:
    machine_config() = default;
};

// Global placeholder passed to core constructors by AppleBox adapters.
inline const machine_config g_shim_mconfig{};

// ---------------------------------------------------------------------------
// device_state_entry — minimal stand-in for the state interface. The cores'
// state_import/export switch on index(); adapters call them directly with a
// synthetic entry.
// ---------------------------------------------------------------------------
class device_state_entry {
public:
    explicit device_state_entry(int index) : index_(index) {}
    int index() const { return index_; }

private:
    int index_ = 0;
};

// Well-known state indices used by cores' state_export.
enum {
    STATE_GENPC = -1,
    STATE_GENPCBASE = -2,
    STATE_GENFLAGS = -3,
};

// Returned by state_add(); all modifiers are fluent no-ops.
class state_entry_builder {
public:
    state_entry_builder& callimport() { return *this; }
    state_entry_builder& callexport() { return *this; }
    state_entry_builder& noshow() { return *this; }
    state_entry_builder& formatstr(const char*) { return *this; }
    state_entry_builder& mask(u64) { return *this; }
};

// ---------------------------------------------------------------------------
// devcb_write_line — callback line binder. Cores drive e.g. SYNC through it.
// ---------------------------------------------------------------------------
class devcb_write_line {
public:
    using func_t = void (*)(void* ctx, int state);

    explicit devcb_write_line(device_t&) {}

    bool isunset() const { return fn_ == nullptr; }
    void operator()(int state) {
        if (fn_) fn_(ctx_, state);
    }
    // MAME returns a binder object from bind(); AppleBox adapters instead call
    // set_callback directly.
    devcb_write_line& bind() { return *this; }
    void set_callback(void* ctx, func_t fn) {
        ctx_ = ctx;
        fn_ = fn;
    }

private:
    void* ctx_ = nullptr;
    func_t fn_ = nullptr;
};

// ---------------------------------------------------------------------------
// address_space / memory_access — placeholders. AppleBox always installs a
// custom memory interface, so mi_default's specific handles are never used at
// runtime; they only need to compile.
// ---------------------------------------------------------------------------
class address_space_config {
public:
    address_space_config() = default;
    address_space_config(const char* name, int endian, int dataWidth,
                         int addrWidth, int addrShift = 0,
                         void* internalMap = nullptr)
        : m_name(name), m_endian(endian), m_data_width(dataWidth),
          m_addr_width(addrWidth), m_addr_shift(addrShift) {
        (void)internalMap;
    }

    const char* m_name = "";
    int m_endian = ENDIANNESS_LITTLE;
    int m_data_width = 8;
    int m_addr_width = 16;
    int m_addr_shift = 0;
};

// Dummy "specific" memory handle. read/write abort: custom memory interfaces
// must be installed before execution.
struct shim_memory_specific {
    u8 read_interruptible(offs_t) {
        std::fprintf(stderr, "mame_shim: default memory interface used\n");
        std::abort();
    }
    void write_interruptible(offs_t, u8) {
        std::fprintf(stderr, "mame_shim: default memory interface used\n");
        std::abort();
    }
};

template <int AddrWidth, int DataShift, int AddrShift, int Endian>
struct memory_access {
    using specific = shim_memory_specific;
};

class address_space {
public:
    explicit address_space(const address_space_config& cfg) : cfg_(cfg) {}
    int addr_width() const { return cfg_.m_addr_width; }
    template <typename T>
    void specific(T&) {}

private:
    address_space_config cfg_;
};

using space_config_vector =
    std::vector<std::pair<int, const address_space_config*>>;

// ---------------------------------------------------------------------------
// device_t — root of the shimmed device hierarchy.
// ---------------------------------------------------------------------------
class device_t {
public:
    device_t(const machine_config&, device_type type, const char* tag,
             device_t* owner, u32 clock)
        : type_(type), tag_(tag ? tag : ""), owner_(owner), clock_(clock) {}
    virtual ~device_t() = default;

    const char* tag() const { return tag_.c_str(); }
    u32 clock() const { return clock_; }

    virtual void device_start() {}
    virtual void device_reset() {}

private:
    device_type_token type_;
    std::string tag_;
    device_t* owner_;
    u32 clock_;
};

// device_memory_interface is a mixin in MAME; the cores only use its
// space_config_vector typedef and a few queries which we place on cpu_device.
class device_memory_interface {
public:
    using space_config_vector = ::space_config_vector;
};

// ---------------------------------------------------------------------------
// cpu_device — the base the vendored cores derive from. Provides the
// execute/state/save-item surface as compile-time no-ops or simple fields.
// ---------------------------------------------------------------------------
class cpu_device : public device_t {
public:
    cpu_device(const machine_config& mconfig, device_type type,
               const char* tag, device_t* owner, u32 clock)
        : device_t(mconfig, type, tag, owner, clock) {}

    // ---- execute interface ----
    virtual u32 execute_min_cycles() const noexcept { return 1; }
    virtual u32 execute_max_cycles() const noexcept { return 1; }
    virtual void execute_run() = 0;
    virtual void execute_set_input(int, int) {}
    virtual bool execute_input_edge_triggered(int) const noexcept {
        return false;
    }
    virtual bool cpu_is_interruptible() const { return false; }

    // ---- memory / state / disasm interface virtuals the cores override ----
    virtual space_config_vector memory_space_config() const { return {}; }
    virtual void state_import(const device_state_entry&) {}
    virtual void state_export(const device_state_entry&) {}
    virtual void state_string_export(const device_state_entry&,
                                     std::string&) const {}
    virtual std::unique_ptr<util::disasm_interface> create_disassembler() {
        return nullptr;
    }

    void set_icountptr(int& icount) { icountptr_ = &icount; }
    s64 total_cycles() const { return total_cycles_; }

    // ---- adapter-facing helpers (not part of MAME's API) ----
    // Run the core for `cycles`; returns cycles actually consumed.
    s64 shim_run(s64 cycles) {
        stolen_ = 0;
        *icountptr_ = static_cast<int>(cycles);
        execute_run();
        s64 consumed = cycles - *icountptr_ - stolen_;
        total_cycles_ += consumed;
        return consumed;
    }
    // Abort the current timeslice (a device scheduled an event mid-slice);
    // the core stops at the next resumable point and shim_run returns the
    // cycles actually executed.
    void shim_abort_slice() {
        if (icountptr_ && *icountptr_ > 0) {
            stolen_ += *icountptr_;
            *icountptr_ = 0;
        }
    }
    void shim_set_input(int line, int state) { execute_set_input(line, state); }
    void shim_start() { device_start(); }
    void shim_reset() { device_reset(); }
    // Some cores gate behavior on total_cycles() != 0 (e.g. NMI-at-reset).
    void shim_bump_total_cycles() { ++total_cycles_; }

    // ---- memory interface ----
    address_space& space(int index) {
        (void)index;
        static address_space dummy{address_space_config{}};
        return dummy;
    }
    bool has_space(int) const { return false; }
    bool has_configured_map(int) const { return false; }

    // ---- state interface (no-op registration) ----
    template <typename T>
    state_entry_builder state_add(int, const char*, T&) {
        return {};
    }

    // ---- save-state registration (no-op; AppleBox serializes in adapters) --
    template <typename T>
    void save_item_impl(T&, const char*) {}

    // ---- debugger hooks ----
    bool debugger_enabled() const { return false; }
    void debugger_instruction_hook(offs_t) {}
    void debugger_wait_hook() {}
    bool access_to_be_redone() { return false; }
    void standard_irq_callback(int, offs_t) {}

private:
    int* icountptr_ = nullptr;
    s64 total_cycles_ = 0;
    s64 stolen_ = 0;
};

// The cores call save_item(NAME(m_field)) as a member; forward to the no-op
// template method. (AppleBox serializes core state in its adapters instead.)
#define NAME(x) x, #x
#define save_item(...) save_item_impl(__VA_ARGS__)
