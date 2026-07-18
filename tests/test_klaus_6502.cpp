// AppleBox — Klaus Dormann 6502 functional & interrupt test harness.
// Data: tests/data/klaus/6502_functional_test.bin (loaded at $0000, entry
// $0400) and 6502_interrupt_test.bin (loaded at $000a, entry $0400, feedback
// port $BFFC driving IRQ bit 0 / NMI bit 1). Success = trap (jump-to-self)
// at the "success" label; any other trap is a failure. Exits 77 (ctest SKIP)
// when data is absent — fetch with scripts/fetch_test_data.sh.
// SPDX-License-Identifier: MIT
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
using ab::u32;

namespace {

// Success-trap address verified against the pinned functional-test listing
// (bin_files/6502_functional_test.lst: "success ... 3469 : jmp *").
constexpr u32 kFunctionalSuccessPc = 0x3469;

// The interrupt test has no prebuilt binary upstream; when the user supplies
// one, the success address is parsed from its sidecar .lst file: the line
// after the "success" label carries the trap address.
u32 successPcFromListing(const fs::path& lst) {
    std::ifstream in(lst);
    if (!in) return ~0u;
    std::string line;
    bool sawSuccess = false;
    while (std::getline(in, line)) {
        if (sawSuccess) {
            unsigned addr = 0;
            if (std::sscanf(line.c_str(), "%x :", &addr) == 1) return addr;
        }
        // Label line: "success" as the first token, no address column.
        auto pos = line.find("success");
        if (pos != std::string::npos && line.find(':') == std::string::npos &&
            line.find("report_success") == std::string::npos)
            sawSuccess = true;
    }
    return ~0u;
}

class KlausBus final : public ab::BusInterface {
public:
    std::vector<u8> ram = std::vector<u8>(0x10000, 0);
    ab::M6502Core* cpu = nullptr;
    bool interruptPort = false; // enable $BFFC feedback register

    u8 read8(u32 addr, ab::AddrSpace = ab::AddrSpace::Flat) override {
        return ram[addr & 0xffff];
    }
    void write8(u32 addr, u8 val, ab::AddrSpace = ab::AddrSpace::Flat) override {
        addr &= 0xffff;
        ram[addr] = val;
        if (interruptPort && addr == 0xbffc && cpu) {
            cpu->setIrq(0, val & 0x01);
            cpu->setNmi(val & 0x02);
        }
    }
};

// Runs until the CPU traps (spins at one instruction) or the cycle budget is
// exhausted. pc() reports the start-of-instruction PC (m_NPC), which changes
// on every instruction dispatch — if it is stable across two coarse slices,
// confirm the trap by observing it stay constant over 16 single cycles
// (longer than any 6502 instruction). Returns the trap PC, ~0u on timeout.
u32 runToTrap(ab::M6502Core& cpu, long long maxCycles) {
    constexpr long long kSlice = 10'000;
    u32 lastPc = ~0u;
    long long cycles = 0;
    while (cycles < maxCycles) {
        cycles += cpu.run(kSlice);
        u32 pc = cpu.pc();
        if (pc != lastPc) {
            lastPc = pc;
            continue;
        }
        bool trapped = true;
        for (int i = 0; i < 16 && trapped; ++i) {
            cycles += cpu.run(1);
            trapped = cpu.pc() == pc;
        }
        if (trapped) return pc;
        lastPc = cpu.pc();
    }
    return ~0u;
}

bool loadBin(const fs::path& p, std::vector<u8>& ram, std::size_t offset) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::vector<u8> data((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    if (data.empty() || offset + data.size() > ram.size()) return false;
    std::copy(data.begin(), data.end(), ram.begin() + offset);
    return true;
}

int runTest(const fs::path& bin, std::size_t loadOffset, bool interruptPort,
            u32 successPc, const char* label) {
    ab::M6502Core cpu;
    KlausBus bus;
    bus.cpu = &cpu;
    bus.interruptPort = interruptPort;
    cpu.attachBus(bus);

    if (!loadBin(bin, bus.ram, loadOffset)) {
        std::printf("SKIP: %s not found\n", bin.string().c_str());
        return 77;
    }

    cpu.reset();
    cpu.setRegister("SP", 0xff);
    cpu.setRegister("P", 0x34);
    cpu.setRegister("PC", 0x0400);

    u32 trap = runToTrap(cpu, 500'000'000LL);
    if (trap == successPc) {
        std::printf("PASS %s (success trap at $%04x)\n", label, trap);
        return 0;
    }
    if (trap == ~0u)
        std::printf("FAIL %s: no trap within cycle budget (pc=$%04x)\n", label,
                    cpu.pc());
    else
        std::printf("FAIL %s: trapped at $%04x, want $%04x\n", label, trap,
                    successPc);
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    fs::path dir = "tests/data/klaus";
    if (argc > 1) dir = argv[1];
    else if (const char* env = std::getenv("KLAUS_6502_DIR")) dir = env;

    int rcFunc = runTest(dir / "6502_functional_test.bin", 0x0000, false,
                         kFunctionalSuccessPc, "6502_functional_test");

    int rcInt = 77;
    u32 intSuccess = successPcFromListing(dir / "6502_interrupt_test.lst");
    if (intSuccess != ~0u)
        rcInt = runTest(dir / "6502_interrupt_test.bin", 0x000a, true,
                        intSuccess, "6502_interrupt_test");
    else
        std::printf("SKIP: 6502_interrupt_test (no user-assembled bin/lst)\n");

    if (rcFunc == 77 && rcInt == 77) return 77;
    return (rcFunc == 1 || rcInt == 1) ? 1 : 0;
}
