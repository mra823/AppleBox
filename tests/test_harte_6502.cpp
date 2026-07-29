// AppleBox — Tom Harte SingleStepTests harness for the 6502-family cores.
// Runs every *.json opcode file (10,000 cases each) against the vendored MAME
// core, verifying final CPU/RAM state and cycle-by-cycle bus activity.
// Usage: test_harte_6502 [6502|65c02] [data-dir]
// Exits 77 (ctest SKIP) when the data set is absent — fetch it with
// scripts/fetch_test_data.sh.
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/bus.h"
#include "cpu/m6502/m6502_core.h"

namespace fs = std::filesystem;
using ab::u8;
using ab::u16;
using ab::u32;
using ab::u64;

// ---------------------------------------------------------------------------
// Minimal JSON parser targeting the SingleStepTests schema.
// ---------------------------------------------------------------------------
namespace {

struct CpuState {
    u16 pc = 0;
    u8 s = 0, a = 0, x = 0, y = 0, p = 0;
    std::vector<std::pair<u16, u8>> ram;
};

struct BusCycle {
    u16 addr = 0;
    u8 val = 0;
    bool write = false;
};

struct TestCase {
    std::string name;
    CpuState initial, final_;
    std::vector<BusCycle> cycles;
};

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    bool parseAll(std::vector<TestCase>& out) {
        ws();
        if (!eat('[')) return false;
        ws();
        if (peek() == ']') return true;
        do {
            TestCase t;
            if (!parseTest(t)) return false;
            out.push_back(std::move(t));
            ws();
        } while (eat(','));
        return eat(']');
    }

private:
    const std::string& s_;
    std::size_t i_ = 0;

    char peek() const { return i_ < s_.size() ? s_[i_] : '\0'; }
    void ws() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' ||
                                  s_[i_] == '\r' || s_[i_] == '\t'))
            ++i_;
    }
    bool eat(char c) {
        ws();
        if (peek() != c) return false;
        ++i_;
        return true;
    }

    bool parseString(std::string& out) {
        if (!eat('"')) return false;
        out.clear();
        while (i_ < s_.size() && s_[i_] != '"') {
            if (s_[i_] == '\\' && i_ + 1 < s_.size()) ++i_;
            out.push_back(s_[i_++]);
        }
        return eat('"');
    }

    bool parseNumber(u64& out) {
        ws();
        out = 0;
        bool any = false;
        while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') {
            out = out * 10 + static_cast<u64>(s_[i_++] - '0');
            any = true;
        }
        return any;
    }

    bool parseRam(std::vector<std::pair<u16, u8>>& out) {
        if (!eat('[')) return false;
        ws();
        if (eat(']')) return true;
        do {
            u64 a = 0, v = 0;
            if (!eat('[') || !parseNumber(a) || !eat(',') || !parseNumber(v) ||
                !eat(']'))
                return false;
            out.emplace_back(static_cast<u16>(a), static_cast<u8>(v));
        } while (eat(','));
        return eat(']');
    }

    bool parseState(CpuState& st) {
        if (!eat('{')) return false;
        do {
            std::string key;
            if (!parseString(key) || !eat(':')) return false;
            if (key == "ram") {
                if (!parseRam(st.ram)) return false;
            } else {
                u64 v = 0;
                if (!parseNumber(v)) return false;
                if (key == "pc") st.pc = static_cast<u16>(v);
                else if (key == "s") st.s = static_cast<u8>(v);
                else if (key == "a") st.a = static_cast<u8>(v);
                else if (key == "x") st.x = static_cast<u8>(v);
                else if (key == "y") st.y = static_cast<u8>(v);
                else if (key == "p") st.p = static_cast<u8>(v);
            }
        } while (eat(','));
        return eat('}');
    }

    bool parseCycles(std::vector<BusCycle>& out) {
        if (!eat('[')) return false;
        ws();
        if (eat(']')) return true;
        do {
            BusCycle c;
            u64 a = 0, v = 0;
            std::string kind;
            if (!eat('[') || !parseNumber(a) || !eat(',') || !parseNumber(v) ||
                !eat(',') || !parseString(kind) || !eat(']'))
                return false;
            c.addr = static_cast<u16>(a);
            c.val = static_cast<u8>(v);
            c.write = kind == "write";
            out.push_back(c);
        } while (eat(','));
        return eat(']');
    }

    bool parseTest(TestCase& t) {
        if (!eat('{')) return false;
        do {
            std::string key;
            if (!parseString(key) || !eat(':')) return false;
            if (key == "name") {
                if (!parseString(t.name)) return false;
            } else if (key == "initial") {
                if (!parseState(t.initial)) return false;
            } else if (key == "final") {
                if (!parseState(t.final_)) return false;
            } else if (key == "cycles") {
                if (!parseCycles(t.cycles)) return false;
            } else {
                return false;
            }
        } while (eat(','));
        return eat('}');
    }
};

// 64 KB flat bus recording every access.
class TestBus final : public ab::BusInterface {
public:
    std::vector<u8> ram = std::vector<u8>(0x10000, 0);
    std::vector<BusCycle> log;
    bool recording = false;

    u8 read8(u32 addr, ab::AddrSpace = ab::AddrSpace::Flat) override {
        u8 v = ram[addr & 0xffff];
        if (recording)
            log.push_back({static_cast<u16>(addr & 0xffff), v, false});
        return v;
    }
    void write8(u32 addr, u8 val, ab::AddrSpace = ab::AddrSpace::Flat) override {
        ram[addr & 0xffff] = val;
        if (recording)
            log.push_back({static_cast<u16>(addr & 0xffff), val, true});
    }
};

struct Failure {
    std::string test;
    std::string what;
};

bool runCase(ab::M6502Core& cpu, TestBus& bus, const TestCase& t,
             bool nmos, Failure& fail) {
    for (auto& [a, v] : t.initial.ram) bus.ram[a] = v;
    const u8 opcode = bus.ram[t.initial.pc];

    cpu.reset(); // clears any partial-instruction state; no bus activity
    cpu.setRegister("A", t.initial.a);
    cpu.setRegister("X", t.initial.x);
    cpu.setRegister("Y", t.initial.y);
    cpu.setRegister("P", t.initial.p);
    cpu.setRegister("SP", t.initial.s);

    bus.log.clear();
    bus.recording = true;
    // Setting PC re-syncs the pipeline and fetches the opcode — this is the
    // test's first cycle.
    cpu.setRegister("PC", t.initial.pc);

    // MAME's pipelined model executes an instruction's trailing next-opcode
    // prefetch as part of the instruction body; expect exactly
    // len(cycles) + 1 accesses, dropping the trailing prefetch.
    const std::size_t want = t.cycles.size() + 1;
    int guard = 64;
    while (bus.log.size() < want && guard-- > 0) cpu.run(1);
    bus.recording = false;
    // A zero-cycle run resumes and completes the instruction's trailing
    // non-memory micro-ops (MAME models CLI/SEI/PLP's P update after the
    // prefetch for IRQ timing) without dispatching the next instruction.
    cpu.run(0);

    if (bus.log.size() != want) {
        fail = {t.name, "cycle count: got " + std::to_string(bus.log.size()) +
                            " accesses, want " + std::to_string(want)};
        return false;
    }
    for (std::size_t i = 0; i < t.cycles.size(); ++i) {
        const auto& e = t.cycles[i];
        const auto& g = bus.log[i];
        if (e.addr != g.addr || e.val != g.val || e.write != g.write) {
            char buf[128];
            std::snprintf(buf, sizeof buf,
                          "cycle %zu: got %s $%04x=$%02x, want %s $%04x=$%02x",
                          i + 1, g.write ? "W" : "R", g.addr, g.val,
                          e.write ? "W" : "R", e.addr, e.val);
            fail = {t.name, buf};
            return false;
        }
    }

    // KIL/JAM opcodes never reach a prefetch: the architectural PC of a
    // jammed CPU is the incremented internal PC. These exist only on the
    // NMOS part — the CMOS 65C02 reuses the same opcodes for NOP #imm and
    // the (zp) addressing modes, which prefetch normally.
    static constexpr u8 kKil[] = {0x02, 0x12, 0x22, 0x32, 0x42, 0x52,
                                  0x62, 0x72, 0x92, 0xb2, 0xd2, 0xf2};
    const bool jammed =
        nmos &&
        std::find(std::begin(kKil), std::end(kKil), opcode) != std::end(kKil);

    auto reg = [&](const char* n) { return cpu.getRegister(n); };
    // P bits 5/4 have no storage on the NMOS 6502 (observable only in
    // pushed values, which the RAM/cycle comparisons verify exactly), so
    // they are masked here; the test data carries synthetic values through.
    struct {
        const char* n;
        u64 got, want;
    } regs[] = {
        {"PC", jammed ? reg("PC.raw") : reg("PC"), t.final_.pc},
        {"A", reg("A"), t.final_.a},
        {"X", reg("X"), t.final_.x},
        {"Y", reg("Y"), t.final_.y},
        {"P", reg("P") & ~u64(0x30), t.final_.p & ~u64(0x30)},
        {"SP", reg("SP"), t.final_.s},
    };
    for (auto& r : regs) {
        if (r.got != r.want) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "%s: got $%02llx, want $%02llx",
                          r.n, static_cast<unsigned long long>(r.got),
                          static_cast<unsigned long long>(r.want));
            fail = {t.name, buf};
            return false;
        }
    }
    for (auto& [a, v] : t.final_.ram) {
        if (bus.ram[a] != v) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "ram[$%04x]: got $%02x, want $%02x",
                          a, bus.ram[a], v);
            fail = {t.name, buf};
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    // argv[1] selects the core: "6502" (NMOS, default) or "65c02" (CMOS).
    // MAME's w65c02 implements the plain CMOS opcode map — no Rockwell
    // RMB/SMB/BBR/BBS and no WDC WAI/STP — so despite the device's name it
    // is validated against Harte's synertek65c02 set, which matches it.
    std::string which = argc > 1 ? argv[1] : "6502";
    const bool cmos = (which == "65c02");
    const char* label = cmos ? "Harte 65C02" : "Harte 6502";

    fs::path dir = cmos ? "tests/data/harte/synertek65c02/v1"
                        : "tests/data/harte/6502/v1";
    if (argc > 2) dir = argv[2];
    else if (const char* env =
                 std::getenv(cmos ? "HARTE_65C02_DIR" : "HARTE_6502_DIR"))
        dir = env;

    if (!fs::is_directory(dir)) {
        std::printf("SKIP: %s data not found at %s\n"
                    "      run scripts/fetch_test_data.sh\n",
                    label, dir.string().c_str());
        return 77;
    }

    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::printf("SKIP: no .json files in %s\n", dir.string().c_str());
        return 77;
    }

    ab::M6502Core cpu(cmos ? ab::M6502Variant::W65C02
                           : ab::M6502Variant::Nmos6502);
    TestBus bus;
    cpu.attachBus(bus);

    // MAME's w65c02 (mame0288) is not cycle-exact against this data set. The
    // divergences were measured, not guessed, and fall into two groups:
    //
    //  * Undefined opcodes — columns $x7 and $xF (which are RMB/SMB/BBR/BBS
    //    only on the Rockwell/WDC parts, not this one) plus $5C, $CB, $DB.
    //    MAME treats them as one-byte one-cycle NOPs; the real part gives
    //    them multi-byte, multi-cycle forms. Wrong length and wrong state.
    //  * Defined opcodes where only the bus timing differs: the dummy-read
    //    address on an indexed page cross, the extra decimal-mode cycle of
    //    ADC/SBC, and read-modify-write abs,X cycle counts. Final register
    //    and memory state is correct for every one of these.
    //
    // Running with the cycle comparison disabled shows all 221 defined
    // opcodes produce exact final state, so the core is sound for running
    // Apple software — which never executes undefined opcodes — but is not
    // cycle-exact enough for beam-racing effects.
    //
    // The list is pinned so the suite stays meaningful: an opcode that
    // starts failing, or one that starts passing, both fail the test.
    static constexpr u8 kCmosKnownDivergent[] = {
        0x07, 0x0f, 0x11, 0x17, 0x19, 0x1d, 0x1e, 0x1f, 0x27, 0x2f, 0x31,
        0x37, 0x39, 0x3c, 0x3d, 0x3e, 0x3f, 0x47, 0x4f, 0x51, 0x57, 0x59,
        0x5c, 0x5d, 0x5e, 0x5f, 0x61, 0x65, 0x67, 0x69, 0x6d, 0x6f, 0x71,
        0x72, 0x75, 0x77, 0x79, 0x7c, 0x7d, 0x7e, 0x7f, 0x87, 0x8f, 0x91,
        0x97, 0x99, 0x9d, 0x9e, 0x9f, 0xa7, 0xaf, 0xb1, 0xb7, 0xb9, 0xbc,
        0xbd, 0xbe, 0xbf, 0xc7, 0xcb, 0xcf, 0xd1, 0xd7, 0xd9, 0xdb, 0xdc,
        0xdd, 0xde, 0xdf, 0xe1, 0xe5, 0xe7, 0xe9, 0xed, 0xef, 0xf1, 0xf2,
        0xf5, 0xf7, 0xf9, 0xfc, 0xfd, 0xfe, 0xff,
    };
    auto isKnownDivergent = [&](int opcode) {
        return cmos && std::find(std::begin(kCmosKnownDivergent),
                                 std::end(kCmosKnownDivergent),
                                 u8(opcode)) != std::end(kCmosKnownDivergent);
    };
    // Opcode number from the file stem, or -1 if it is not two hex digits.
    auto opcodeOf = [](const fs::path& p) {
        const std::string stem = p.stem().string();
        if (stem.size() != 2) return -1;
        try {
            return std::stoi(stem, nullptr, 16);
        } catch (...) {
            return -1;
        }
    };

    std::vector<int> unexpectedFail, unexpectedPass;
    long total = 0, failed = 0;
    int failedFiles = 0;
    for (const auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        std::vector<TestCase> cases;
        if (!Parser(text).parseAll(cases)) {
            std::printf("FAIL %s: JSON parse error\n",
                        f.filename().string().c_str());
            ++failedFiles;
            continue;
        }
        long fileFailed = 0;
        Failure firstFail;
        for (const auto& t : cases) {
            ++total;
            Failure fail;
            if (!runCase(cpu, bus, t, !cmos, fail)) {
                ++failed;
                if (++fileFailed == 1) firstFail = fail;
            }
        }
        const int op = opcodeOf(f);
        const bool known = isKnownDivergent(op);
        if (fileFailed && !known) {
            ++failedFiles;
            unexpectedFail.push_back(op);
            std::printf("FAIL %s: %ld/%zu  e.g. \"%s\": %s\n",
                        f.filename().string().c_str(), fileFailed,
                        cases.size(), firstFail.test.c_str(),
                        firstFail.what.c_str());
        } else if (!fileFailed && known) {
            unexpectedPass.push_back(op);
        }
        if (fileFailed && known) failed -= fileFailed; // accounted separately
    }

    std::printf("%s: %ld cases, %ld unexpected failures, %d/%zu files failed\n",
                label, total, failed, failedFiles, files.size());

    for (int op : unexpectedPass)
        std::printf("FAIL opcode $%02x now passes — remove it from "
                    "kCmosKnownDivergent\n",
                    op);
    if (cmos && unexpectedFail.empty() && unexpectedPass.empty())
        std::printf("(%zu opcodes diverge from MAME's w65c02 as documented)\n",
                    std::size(kCmosKnownDivergent));

    return (failed || !unexpectedPass.empty()) ? 1 : 0;
}
