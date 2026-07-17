# Execution Plan: "AppleBox" — A Cycle-Accurate Multi-Machine Apple Emulator (Apple I → iMac G3)

*A phased, agent-ready project plan for building a new open-source Apple computer emulator from scratch. Written for an experienced emulator developer (86Box contributor; author of the 80Box 8080/S-100 emulator in C++17/SDL2/Dear ImGui on Fedora KDE; Ghidra BIOS RE experience). Hand this document to an AI coding agent in VS Code as the basis for a `CLAUDE.md`.*

## TL;DR
- **Build a single modular C++20 + SDL2 + Dear ImGui application** on the 86Box device-model pattern, integrating **existing, permissively-licensed CPU cores** (MAME 6502/65C816/PowerPC = BSD-3-Clause per file; Moira 68000/010/020 = MIT; Musashi as 68030/040 fallback = MIT) rather than writing cores from scratch — and validate every core against Tom Harte's SingleStepTests plus Klaus Dormann's functional tests *before* wiring up any machine.
- **Sequence work by rising complexity**: Apple I (6502 + PIA) → Apple II family → 68k Macs (Plus/SE/II) → Apple IIGS → Lisa → PowerPC Macs → iMac G3, because each phase reuses the bus/scheduler/device abstractions and CPU cores built earlier.
- **The single hardest decision is the PowerPC core's license**: every mature PPC-Mac emulator (DingusPPC = GPL-3.0, PearPC = GPL-2.0, QEMU = GPL-2.0, SheepShaver = GPL-2.0+) is copyleft, so to keep a clean permissive codebase you must adapt **MAME's BSD-3-Clause PowerPC core** (601/603/604/750) — otherwise copying DingusPPC device code forces the entire project to GPL.

## Key Findings

### CPU core recommendations (with licenses)
- **6502 / 65C02 (Apple I, II, II+, IIe, IIc, III):** Use MAME's `m6502`/`g65sc02` device family (**BSD-3-Clause** per-file, copyright Aaron Giles/MAMEdev), cycle-accurate and battle-tested. A lightweight alternative is fake6502/lib6502, but those are not fully cycle-accurate on edge cases. Validate with Klaus Dormann's `6502_65C02_functional_tests` and Tom Harte's 6502/65C02 SingleStepTests (JSON, per-opcode, with bus activity).
- **65C816 (Apple IIGS):** MAME's `g65816` core (BSD-3-Clause) is the cleanest reusable option. KEGS/GSplus cores are fast and accurate but structured monolithically and GPL-adjacent. Validate against Tom Harte's 65816 SingleStepTests. Note the IIGS ran the 65C816 at **2.8 MHz (switchable to 1 MHz for Apple II compatibility)** — a deliberate marketing decision to keep it below Macintosh performance, even though the chip was certified to ~4 MHz (Wikipedia, *Apple IIGS*).
- **68000/010/020 (Macs, Lisa):** **Moira** by Dirk Hoffmann (github.com/dirkwhoffmann/Moira), **MIT license**, cycle-accurate with precise inter-instruction timing (memory accesses land on the correct bus cycles), used as the heart of vAmiga. Per the project: *"Moira even surpasses the emulation speed of Musashi, which belongs to the fastest M68k emulators ever developed. The source code of the core emulator is open-source and published under the terms of the MIT License."* Important nuance: **precise inter-instruction timing is only implemented for the 68000 and 68010; 68020 emulation always runs in a simpler cycle mode.**
- **68030/040 + FPU/MMU (SE/30, Quadra, A/UX):** Moira does not fully cover 030/040 MMU/FPU. Use **Musashi** (github.com/kstenerud/Musashi, **MIT license**), which supports 68000–68040 including 68881/68882 FPU hooks and function-code pins, OR adopt MAME's newer microcode-generated m68000 core (BSD-3-Clause, by Olivier Galibert, 99% generated from microcode with perfect intra-instruction timing). The **Snow** emulator (below) is the best modern behavioral reference for Mac-specific 68k quirks.
- **PowerPC 601/603/604/750 (Power Macs, iMac G3):** Adapt **MAME's PowerPC core** (`src/devices/cpu/powerpc/`, header verbatim `// license:BSD-3-Clause // copyright-holders:Aaron Giles`) — a DRC/JIT core (DRCUML) with device classes for PPC601/602/603/603e/603r/604/740/750(G3)/G4 and PVRs for each. **This is the only mature, accurate, permissively-licensed PPC core.** DingusPPC (GPL-3.0) is the best *reference* for PPC-Mac chipset behavior but its code cannot be copied into a permissive project.

### Existing emulators to learn from (license implications)
**Permissive / directly reusable (isolate in `cpu/` or `devices/`, preserve headers):**
- MAME — BSD-3-Clause per file (>90% incl. core files), but GPL-2.0+ as an aggregate binary; extract only the specific CPU/device files you need with their `// license:BSD-3-Clause` headers intact and do NOT link the whole of MAME.
- Moira — MIT (68000/010/EC020/020).
- Musashi — MIT (68000–68040 + FPU).
- Snow — MIT. Written in Rust by **Thomas van der Velden (twvd)**; emulates Macintosh 128K/512K/Plus/SE/Classic/II up through SE/30, with recent A/UX support. Explicitly aims to *"emulate the Macintosh on a hardware level as much as possible, as opposed to emulators that patch the ROM or intercept system calls."* Best behavioral reference for the IWM/SWIM, GCR floppy formats, and compact-Mac glue — though as Rust it's a reference, not a code-copy source for a C++ project.
- Clemens IIGS (samkusin/clemens_iigs, C/C++) and gsplus/KEGS (mixed) — good IIGS references.

**GPL — reference/documentation only, do NOT copy into a permissive build:**
- AppleWin / LinApple (GPL), Basilisk II / SheepShaver (GPL-2.0+; SheepShaver's "Kheperix" JIT works only on x86/x64, has no MMU or supervisor mode — deliberately not hardware-accurate), LisaEm (GPL; uses the Generator 68k core tied intimately to its custom MMU), DingusPPC (GPL-3.0), PearPC (GPL-2.0, dormant since 2011, generic G3/G4 not per-model), QEMU (GPL-2.0; per-file SPDX varies).

**Most accurate chipset emulation, per family:** Apple II → AppleWin + MAME + Applesauce/WOZ handling; IIGS → KEGS/GSplus + Clemens + MAME; 68k Mac → Snow (new, hardware-level, no ROM patching) + MAME `mac` drivers; Lisa → LisaEm (the only fully functional Lisa emulator); PowerPC → DingusPPC (accuracy-focused, Old World) + QEMU (mac99/g3beige, New World, Open Firmware).

### UI/UX and architecture
86Box uses a **Qt** front-end over a C emulator core, with a `device_t` model and `device_config_t` config descriptors; machines are chosen by class then model, and a device's `available()` callback gates selection on ROM presence (`rom_present()`). Mirror this design: a machine-configuration dialog with CPU-family category dropdowns (6502 / 65C816 / 68000 / 68020–040 / PowerPC) plus RAM/video/storage/sound/ports tabs. Given the user's 80Box background, use **Dear ImGui on SDL2** rather than Qt (justification below).

## Details

### Recommended tech stack (and justification)
- **Language: C++20** (concepts, `std::span`, `constexpr`, designated initializers) — matches DingusPPC, Moira, and modern MAME, and extends the user's C++17 experience. DingusPPC and modern MAME already require C++20.
- **UI: Dear ImGui (docking branch) on SDL2 + OpenGL3.** Rationale: the user already ships this exact stack in 80Box; ImGui is ideal for emulator debug UIs (memory/register/disassembly windows), and an 86Box-style machine-config dialog is a straightforward modal ImGui window with combo boxes. Qt is justified only if native-looking dialogs are a hard requirement; it is heavier to build cross-platform. SDL2 is recommended over SDL3 initially because it is broadly packaged on Fedora and every reference emulator targets it; SDL3 is a viable later migration (gsplus already moved to SDL3).
- **Build: CMake ≥ 3.20** with `CMakePresets.json`, Ninja generator, and submodules/FetchContent for vendored cores — mirroring DingusPPC (CMake + submodules for CubeB/Capstone/SDL2) and 86Box.
- **Audio: SDL2 audio or CubeB** (DingusPPC uses CubeB).
- **Disassembly: vendor Capstone** for PowerPC (as DingusPPC does) and use per-core disassemblers (Moira and MAME ship their own) for 6502/65816/68k — directly serving the user's Ghidra-style debugging workflow.

### Repository structure (proposal)
```
applebox/
  CMakeLists.txt  CMakePresets.json  CLAUDE.md
  src/
    core/         # scheduler, bus, device base, save-state, config
    cpu/
      m6502/      # vendored MAME 6502 (BSD) + adapter
      m65816/     # vendored MAME 65816 (BSD) + adapter
      m68k/       # Moira (MIT) primary + Musashi (MIT) for 030/040
      ppc/        # MAME PPC core (BSD) + adapter
    machines/     # per-machine assembly (apple1.cpp, apple2e.cpp, macplus.cpp, ...)
    devices/      # pia6821, via6522, iwm, swim, scc8530, ncr5380, asc, doc5503, mega2, vgc, ...
    video/        # ntsc artifact, mac framebuffer, vgc, nubus cards, ati_rage
    media/        # disk image loaders (woz, dsk, po, 2mg, dc42, dart, moof, hfs, iso)
    ui/           # imgui config dialog, debugger, main window
    debugger/     # multi-CPU disasm, breakpoints, memory view
  thirdparty/     # capstone, imgui, SDL2 (submodules)
  tests/          # cpu test harness, rom-boot smoke tests
  roms/           # NOT committed; user-supplied, 86Box-style layout
```

### CPU abstraction and scheduler
- Define an abstract **`ICpuCore`** interface: `reset()`, `run(int64_t cycles)`, `irq(level)`, `nmi()`, register get/set, `disassemble(addr)`, plus a **`BusInterface`** callback object (`read8/16/32`, `write8/16/32`, with address-space / function-code support for the 68k). This lets heterogeneous cores plug in behind one interface — the 86Box plugin-style device model applied to CPUs.
- **Scheduler:** a single 64-bit master-clock timeline (86Box itself moved to 64:32 fixed-point timers to reduce wraparound). Express every device clock as a rational ratio of the master clock. Run the CPU in slices to the next scheduled event; devices register callbacks. This uniformly spans the **1 MHz 6502 up to the 233 MHz PowerPC 750** in the iMac G3. Where cycle accuracy matters (Apple II video beam racing, Mac IWM/DMA contention, Amiga-style bus timing on Moira), drive the CPU per-cycle or per-memory-access using Moira's inter-instruction timing hooks.
- **Save states:** serialize CPU state plus each device's state struct via a versioned visitor; forbid pointer/coroutine state in cores (a known MAME state-saving constraint).

### Per-machine hardware to emulate
- **Apple I:** MOS 6502 @ ~1 MHz; Motorola 6821 PIA (KBD $D010, KBDCR $D011, DSP $D012, DSPCR $D013); terminal/video section with the "Data Available" handshake. Woz Monitor ROM at $FF00; optional Apple 1 BASIC at $E000. Trivial — the ideal first-boot target.
- **Apple II / II+ / IIe / IIc:** 6502 (65C02 on enhanced IIe / IIc); softswitches; **Disk II controller (Woz state machine)** reading WOZ bitstreams with MC3470 read-pulse behavior; language card / 80-column card; MMU + IOU custom ICs (IIe); NTSC artifact color video; 1-bit speaker; Mockingboard (dual 6522 + AY-3-8910) and Phasor. Slots for Super Serial Card, Videoterm, memory expansion.
- **Apple III:** 6502 with extended addressing / bank register custom logic; SOS; needs its own ROM set and custom glue — MAME's apple3 driver is the reference.
- **Apple IIGS:** WDC 65C816 @ 2.8 MHz (switchable 1 MHz); **Mega II** (Apple II compatibility); **VGC** (Video Graphics Controller — Super Hi-Res 320×200 / 640×200, 4096-color palette, per-scanline palettes yielding up to 3200 colors); **Ensoniq 5503 DOC** (designed by Bob Yannes, creator of the SID; 32 oscillators, Apple firmware pairs them into 16 voices; 64 KB of dedicated sound RAM — Apple wired 64K of the 128K the DOC can address) driven via the Sound GLU; **IWM/SmartPort** floppy; two Zilog 8530 SCC serial ports; ADB; ROM 00/01 (128 KB) and ROM 03 (256 KB).
- **Lisa:** 68000 @ 5 MHz (an 8 MHz-capable part underclocked to synchronize with video, giving ~2.5 MHz effective throughput; 1 MB RAM); **custom proprietary MMU** (segment/hardware); video state ROM (which doubles as the serial-number chip); two 6522 VIAs; COPS 421 microcontroller (keyboard/mouse/soft-power/clock); a dedicated 6504 CPU + ROM for floppy control; Twiggy (FileWare) then Sony 400K drives; ProFile parallel-port hard disk; optional AMD 9512 math coprocessor. LisaEm is the only working reference (GPL — behavior only). The hardest 68k target, due to the bespoke MMU.
- **68k Macs (128K → Quadra):** 68000–68040; **IWM then SWIM** floppy (GCR 400/800K, MFM 1.44 MB SuperDrive); one or two **6522 VIAs**; **Zilog 8530 SCC** serial (which also carries mouse quadrature on compact Macs); **NCR 5380** SCSI (later **53C96** on Quadra, 32-bit); ADB (via 6522 + later Egret/Cuda 68HC05 microcontrollers); **Apple Sound Chip (ASC)** on SE/30, Portable, II-family; built-in framebuffers + NuBus video cards; RTC/PRAM; ROM sizes 64K/128K/256K/512K/1MB. Machine-ID/glue ASICs by model: BBU (SE), GLUE (SE/30), V8/Eagle/Spice/Sonora (LC family), RBV+MDU (IIsi), DAFB (Quadra 700), OSS (IIfx), PrimeTime/PrimeTime II (LC/Quadra 630). ADB controllers: Egret (LC, IIsi, Classic II), Cuda (Color Classic, most 68040 machines, all PPC 601/603/604). FPU: 68881/68882 external, on-chip from the 68040. MMU: none on 68000 Macs; 68851 optional on the II; on-chip 030/040. Snow is the best hardware-level reference (MIT).
- **PowerPC Macs (6100 → iMac G3):** PPC 601 (NuBus; 6100/7100/8100), then 603/604 (PCI; 7200/7500/8500), then 750/G3 (Beige G3, iMac). I/O ASICs by generation: **AMIC** (NuBus PDM), **GrandCentral** (first PCI), **O'Hare/Heathrow/Paddington** (G3/iMac). Sound: **AWACS/Screamer**. **Open Firmware** (IEEE 1275; OpenBIOS, GPL-2, is a reference implementation). **Old World ROM** (Toolbox in ROM) vs **New World ROM** (Mac OS ROM file loaded from disk). The **iMac G3** (Rev. A introduced May 6, 1998, shipping Aug 15, 1998 with Mac OS 8.1) uses a **233 MHz PowerPC 750** with 512K backside L2 cache, 32 MB RAM, a 4.0 GB EIDE drive, and **ATI Rage IIc graphics with 2 MB VRAM** (EveryMac); it replaces ADB with **USB**. DingusPPC is the best chipset reference (GPL-3.0 — study, don't copy); QEMU mac99/g3beige for New World and Open Firmware behavior.

### Disk/media formats
- **Apple II:** WOZ 2.1 (public-domain spec, bit-accurate, models copy protection + MC3470 behavior — essential for accurate Apple II emulation), DSK/DO/PO (sector images), NIB (nibble), 2MG (with header), A2R (flux). 5.25″ = 140K; 3.5″ = 800K.
- **Mac/Lisa:** DiskCopy 4.2 (PRONOM fmt/625), DART, **MOOF** (Applesauce's Mac bit-accurate format, public spec — the Mac counterpart to WOZ), raw .dsk/.image, A2R flux; HFS/HFS+ volumes; hard-disk raw images to 2 GB; CD-ROM ISO. Lisa uses 400K/800K DiskCopy 4.2.
- **Volume mounting:** ProDOS (≤32 MB/partition), HFS/HFS+ for hard-disk images; ISO9660 for CD.
- **86Box-style ROM organization:** a `roms/` tree with per-machine subfolders and a manifest (name, size, CRC/SHA) checked by an `available()`-style callback before a machine can be selected — exactly how 86Box's `rom_present()` gate works. This also cleanly handles the odd/even split Lisa ROMs (boot.hi/boot.lo) and multi-file Mac ROM sets.

### UI category-dropdown structure (mirroring 86Box machine-type organization)
**Machine Type (CPU family) dropdown → Machine Model dropdown:**
- **6502:** Apple I; Apple II; Apple II+; Apple IIe; Apple IIe Enhanced; Apple IIc; Apple IIc+; Apple III
- **65C816:** Apple IIGS (ROM 00); Apple IIGS (ROM 01); Apple IIGS (ROM 03)
- **68000:** Lisa 2; Macintosh 128K; 512K; Plus; SE; Classic; Portable
- **68020:** Macintosh II; Macintosh LC
- **68030:** Mac IIx/IIcx/IIci/IIsi; SE/30; LC III; Classic II
- **68040:** Quadra/Centris 610/650/700/840AV; LC 475
- **PowerPC 601:** Power Mac 6100/7100/8100
- **PowerPC 603/604:** Power Mac 7200/7500/8500; Performa
- **PowerPC 750 (G3):** Power Mac G3 Beige; iMac G3

Each selected model exposes 86Box-style tabs: **Machine** (model, CPU speed, FPU present, RAM size), **Video** (built-in / NuBus / PCI card, VRAM), **Storage** (floppy drives + controller, SCSI/IDE disks, CD-ROM), **Sound** (speaker / Mockingboard / ASC / DOC / AWACS), **Ports** (serial/SCC, ADB/USB, LocalTalk), **Input** (keyboard/mouse/joystick).

### Testing strategy
- **CPU unit tests first (per-phase gate):** run Tom Harte `SingleStepTests` (JSON per-opcode with bus activity) for 6502, 65C02, 65816, and 68000; Klaus Dormann's `6502_65C02_functional_tests` and `6502_interrupt_test`; and DingusPPC's `testppc` methodology (or an equivalent PPC opcode suite) for PowerPC. A core is not "done" until it passes.
- **ROM boot smoke tests (per machine, headless, scripted):** Apple I reaches the Woz Monitor `\` prompt; Apple II+ boots to the Applesoft `]` prompt; Apple IIe boots DOS 3.3 from a .dsk; Mac Plus boots System 6 from an 800K DiskCopy image and reaches Finder; Mac II boots System 7; IIGS boots GS/OS; Lisa boots Lisa Office System 7/7; Power Mac 6100 boots System 7.1.2 from the Disk Tools floppy; iMac G3 reaches the Open Firmware prompt then boots Mac OS 8.
- **Frame-hash / screenshot regression** for video accuracy (NTSC artifact colors, Super Hi-Res, Mac 1-bit framebuffer).
- **CI:** GitHub Actions matrix (Fedora/Ubuntu GCC + Clang, Windows MSVC/clang, macOS) building with CMake presets and running the CPU suites on every push; cache the large Harte JSON sets.

## Recommendations (staged execution plan)

Hand this to the AI agent as `CLAUDE.md` phases. Each phase has acceptance criteria; do not advance until they are met. ("Unit" = one focused work-block; treat as relative ordering/complexity, not a calendar estimate.)

**Phase 0 — Skeleton (1 unit).** CMake project, ImGui+SDL2 window, `ICpuCore`/`BusInterface`/`Device`/`Scheduler` abstractions, save-state visitor, ROM manifest loader, empty machine-config dialog, and a **per-file license audit of all vendored code.** *Accept:* window opens on Fedora; empty scheduler runs; CI green; license audit committed.

**Phase 1 — 6502 core + Apple I (1 unit).** Vendor MAME 6502 (BSD) behind the adapter; pass Klaus + Harte 6502 tests. Implement the 6821 PIA + terminal. *Accept:* Apple I boots the Woz Monitor and runs a hand-entered hex program; 100% Harte 6502 pass.

**Phase 2 — Apple II family (2 units).** Softswitches, NTSC artifact video, speaker, Disk II Woz state machine + WOZ/DSK/PO/2MG loaders, 65C02 for enhanced models, language/80-col cards, Mockingboard. *Accept:* Apple II+ → Applesoft; IIe Enhanced boots DOS 3.3 and ProDOS; a copy-protected WOZ title boots; frame-hash matches reference for artifact color.

**Phase 3 — 68k core + first Macs (3 units).** Integrate Moira (MIT); pass Harte 68000 tests. Build 6522 VIA, 8530 SCC, IWM, NCR 5380, Mac framebuffer, RTC/PRAM, ADB. *Accept:* Mac Plus boots System 6 from an 800K DiskCopy image to Finder; Mac SE boots System 7; a SCSI disk image mounts.

**Phase 4 — Apple IIGS (2 units).** Integrate MAME 65816 (BSD); pass Harte 65816 tests. Mega II, VGC (Super Hi-Res), Ensoniq 5503 DOC + Sound GLU, IWM/SmartPort, dual SCC, ADB. *Accept:* IIGS ROM 01 and ROM 03 boot GS/OS; DOC audio plays a .mod/Soundsmith file; Apple II compatibility mode runs.

**Phase 5 — Lisa (2 units).** 68000 + bespoke Lisa MMU, dual VIA, COPS, 6504 floppy subsystem, ProFile, Twiggy/Sony. Study LisaEm (GPL) for behavior only. *Accept:* Lisa boots Lisa Office System 7/7; ProFile boots; Service Mode reachable.

**Phase 6 — Later 68k Macs (2 units).** Add 68020/030/040 (Moira for 020; Musashi or the MAME microcode core for 030/040 with 68881/68882 FPU + 030/040 MMU), SWIM (1.44 MB), 53C96, ASC, Egret/Cuda, NuBus video, DAFB. *Accept:* SE/30 boots System 7 with FPU; Quadra 650 boots System 7.5; A/UX boots on a IIci/Quadra (validates the MMU).

**Phase 7 — PowerPC (4 units, highest risk).** Adapt MAME's PowerPC core (BSD) for 601/603/604/750; build AMIC/GrandCentral/Heathrow/Paddington, AWACS/Screamer, Open Firmware, and Old- vs New-World ROM handling. Study DingusPPC (GPL) and QEMU (GPL) for behavior only. *Accept:* Power Mac 6100 boots System 7.1.2; Beige G3 boots Mac OS 8; iMac G3 reaches the Open Firmware prompt then boots Mac OS 8.6 (USB keyboard/mouse, ATI Rage framebuffer).

**Phase 8 — Polish (2 units).** Integrated multi-CPU debugger (disassembly for all families via Capstone + native disassemblers; breakpoints/watchpoints/memory view — leverage the user's Ghidra workflow), save states across all machines, config persistence, optional SDL3 migration, optional Qt front-end, documentation.

**Thresholds that change the plan:**
- If MAME's PPC core proves too entangled with MAME infrastructure to extract cleanly, fall back to a clean-room interpreter using DingusPPC/QEMU as *documentation only*, accepting a longer Phase 7.
- If a permissive license is *not* a hard requirement, adopting DingusPPC device code (GPL-3.0) directly could roughly halve Phase 7 — but this forces the whole project to GPL-3.0. **Decide the licensing posture before Phase 7.**
- If cycle-exact Apple II beam-racing demos must run, budget extra time in Phase 2 for per-cycle video; MAME's Apple II video is not cycle-accurate even though its 6502 is.

## Caveats
- **ROM legality:** Apple ROMs (Woz Monitor, Apple II, Mac Toolbox, IIGS, New World Mac OS ROM) are Apple copyright and cannot be distributed. The app must require user-supplied ROMs with a manifest/hash check, exactly as KEGS, Snow, Basilisk II, and 86Box all do.
- **License containment is the central architectural constraint.** Mixing GPL code (AppleWin, LisaEm, DingusPPC, Basilisk II, PearPC, QEMU) into a permissive codebase forces the whole work to GPL. Keep permissive cores (MAME BSD files with headers intact, Moira MIT, Musashi MIT) isolated in `cpu/`, and treat all GPL emulators as documentation/reference only. This audit belongs in Phase 0.
- **MAME reuse nuance:** individual MAME files are BSD-3-Clause but the aggregate binary is GPL-2.0+. Extract only the specific CPU/device files you need, preserve their `// license:BSD-3-Clause` headers, and never link the whole of MAME.
- **Moira coverage seam:** Moira is cycle-accurate for 68000/010 (precise inter-instruction timing) but runs the 68020 in a simpler cycle mode and does not implement 030/040 MMU/FPU. Those later Macs (and A/UX) require Musashi or the MAME microcode core, which have different timing fidelity — expect a fidelity seam at Phase 6.
- **PowerPC accuracy vs speed:** MAME's PPC core is a dynamic recompiler tuned for arcade accuracy, not cycle-exact Mac timing; DingusPPC is a pure interpreter and, per Infinite Mac's author, "extremely slow (as in, more than 3 minutes to go from boot to the Finder running)" precisely because it prioritizes accuracy. Expect an explicit speed/accuracy tradeoff decision in Phase 7.
- **Lisa and PowerPC are the two riskiest phases** — Lisa because of its undocumented custom MMU (only LisaEm, GPL, has fully solved it) and PowerPC because of the license bind plus chipset complexity. Schedule both late and budget generously.
- **Source quality:** some chip-level details here come from community wikis and forums (68kMLA, E-Maculation, Applefritter, preterhuman.net) rather than primary Apple documentation. The AI agent should cross-check hardware behavior against the *Guide to the Macintosh Family Hardware* and the MAME/DingusPPC source before implementing.