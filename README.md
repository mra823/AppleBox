# AppleBox

A cycle-accurate, multi-machine Apple computer emulator — Apple I through
iMac G3 — with an 86Box-style machine-configuration UI.

**Status: Phase 1 complete — Apple I.** The vendored MAME 6502 core passes
all 2,560,000 SingleStepTests cases and the Klaus Dormann functional test;
the Apple I (6502 @ 1.023 MHz, MC6821 PIA, terminal) boots the Woz Monitor
(user-supplied ROM). See [CLAUDE.md](CLAUDE.md) for the full phased plan.

## Stack

- C++20, CMake ≥ 3.20 + Ninja
- SDL2 + OpenGL3 + Dear ImGui (docking)
- Vendored permissive CPU cores per phase (MAME 6502/65816/PPC — BSD-3-Clause;
  Moira, Musashi — MIT). See [THIRDPARTY.md](THIRDPARTY.md) for the license audit.

## Building (Fedora)

```sh
sudo dnf install cmake ninja-build gcc-c++ SDL2-devel mesa-libGL-devel python3
git clone --recursive <repo-url> && cd applebox
cmake --preset debug
cmake --build --preset debug
./scripts/fetch_test_data.sh  # optional: CPU validation suites (~600 MB)
ctest --preset debug          # unit + CPU tests (data-less tests skip)
./build/debug/applebox        # launch UI
```

## ROMs

Apple ROMs are copyrighted and never distributed here. Place your own dumps
under `roms/<machine>/` — see [roms/README.md](roms/README.md).

## License

MIT (see [LICENSE](LICENSE)). Vendored third-party code retains its original
permissive license and headers; GPL emulators are used strictly as behavioral
references, never as code sources.
