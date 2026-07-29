# Third-Party Code Audit

AppleBox is MIT-licensed. Every piece of vendored or linked third-party code is
recorded here with its origin and license. **Only permissive licenses (MIT,
BSD-2/3-Clause, Zlib, public domain) may be vendored.** GPL projects (AppleWin,
LisaEm, DingusPPC, Basilisk II, PearPC, QEMU, KEGS/GSplus) are documentation /
behavioral references only — their code must never be copied into this tree.

| Component | Path | Origin | License | Copyright |
|-----------|------|--------|---------|-----------|
| Dear ImGui (docking) | `thirdparty/imgui/` (submodule) | github.com/ocornut/imgui | MIT | Omar Cornut |
| SDL2 | system library (linked) | libsdl.org | Zlib | Sam Lantinga et al. |
| SHA-256 | `src/core/sha256.cpp` | original implementation for AppleBox (from the FIPS 180-4 public specification) | MIT (project) | AppleBox contributors |
| MAME m6502 core | `src/cpu/m6502/vendor/` | github.com/mamedev/mame @ `mame0288`, `src/devices/cpu/m6502/` (m6502.cpp/.h, m6502d.cpp/.h, m6502make.py, om6502.lst, dm6502.lst) | BSD-3-Clause (per-file headers intact) | Olivier Galibert |
| MAME w65c02 core | `src/cpu/m6502/vendor/` | same tag, `w65c02.cpp/.h`, `w65c02d.cpp/.h`, `ow65c02.lst`, `dw65c02.lst` | BSD-3-Clause (per-file headers intact) | Olivier Galibert |

### Local modifications to vendored MAME m6502

All changes are in `om6502.lst`, marked `APPLEBOX MODIFICATION`, and were
made to match real-silicon behavior verified by SingleStepTests 65x02
(2,560,000 cases, 100% pass):

- `ane_imm` / `lxa_imm`: unstable-opcode "magic" constant `0xEE` (64doc
  measurement) instead of upstream's implicit `0xFF`.
- `las_aby`: classic `A = X = S = value & S` instead of upstream's
  `A = value|0x51, X = 0xFF`.
- `kil_non`: no PC increment after the dummy operand read (jammed-CPU PC
  stops after the opcode fetch, per visual6502).

### Accuracy of the vendored MAME w65c02

`w65c02.cpp/.h` are vendored **unmodified**. The core is not cycle-exact
against SingleStepTests; `tests/test_harte_6502.cpp` pins the 84 diverging
opcodes so any change in either direction fails the suite. Measured, not
assumed:

- **Undefined opcodes** — columns `$x7`/`$xF` (RMB/SMB/BBR/BBS only on the
  Rockwell and WDC parts, not this one) plus `$5C`, `$CB`, `$DB`. MAME makes
  them one-byte one-cycle NOPs; the real part gives them multi-byte,
  multi-cycle forms, so both length and final state differ.
- **Defined opcodes, timing only** — the dummy-read address on an indexed
  page cross, ADC/SBC's extra decimal-mode cycle, and read-modify-write
  `abs,X` cycle counts.

With the cycle comparison disabled, all 221 defined opcodes produce exact
final state, so the core runs Apple software correctly (no Apple ROM
executes an undefined opcode) but is not cycle-exact enough for beam-racing
effects. MAME's `g65sc02` is a pure alias of `w65c02` with no timing
differences, so it is not an alternative.

Note the naming trap: despite the device name, MAME's `w65c02` implements the
plain CMOS opcode map, so the matching SingleStepTests set is
`synertek65c02`, not `wdc65c02`.

The core is compiled against `src/cpu/mame_shim/emu.h`, an original AppleBox
header (MIT) that fakes the minimal MAME device-model surface; no other MAME
infrastructure is linked. Dispatch tables (`m6502*.hxx`) are generated at
build time by MAME's own `m6502make.py`.

## Planned (future phases — audit before vendoring)

| Component | Purpose | License | Notes |
|-----------|---------|---------|-------|
| MAME g65816 | 65C816 core (Phase 4) | BSD-3-Clause per file | Keep `// license:BSD-3-Clause` headers intact; never link whole MAME |
| Moira | 68000/010/020 core (Phase 3) | MIT | Dirk Hoffmann |
| Musashi | 68030/040 + FPU (Phase 6) | MIT | Karl Stenerud |
| MAME PowerPC | PPC 601/603/604/750 (Phase 7) | BSD-3-Clause per file | Aaron Giles |
| Capstone | PPC disassembly (Phase 7–8) | BSD-3-Clause | |

## ROMs

Apple ROMs are copyrighted and are **never** committed or distributed. Users
supply their own under `roms/`, validated against `roms/manifest` hashes.

## Test data (fetched, never committed)

| Data | Source | License | Use |
|------|--------|---------|-----|
| SingleStepTests 65x02 (6502) | github.com/SingleStepTests/65x02 | MIT | CPU validation (`tests/data/harte/`) |
| Klaus Dormann 6502 functional tests | github.com/Klaus2m5/6502_65C02_functional_tests (pinned commit) | GPL-3.0 | test data only, executed by the emulator under test; not linked or distributed (`tests/data/klaus/`) |
