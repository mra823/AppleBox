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

The core is compiled against `src/cpu/mame_shim/emu.h`, an original AppleBox
header (MIT) that fakes the minimal MAME device-model surface; no other MAME
infrastructure is linked. Dispatch tables (`m6502*.hxx`) are generated at
build time by MAME's own `m6502make.py`.

## Planned (future phases — audit before vendoring)

| Component | Purpose | License | Notes |
|-----------|---------|---------|-------|
| MAME g65sc02 | 65C02 core (Phase 2) | BSD-3-Clause per file | Keep `// license:BSD-3-Clause` headers intact; never link whole MAME |
| MAME g65816 | 65C816 core (Phase 4) | BSD-3-Clause per file | same |
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
