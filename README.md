# AppleBox

A cycle-accurate, multi-machine Apple computer emulator — Apple I through
iMac G3 — with an 86Box-style machine-configuration UI.

**Status: Phase 0 (skeleton).** No machines are emulated yet. See
[CLAUDE.md](CLAUDE.md) for the full phased execution plan.

## Stack

- C++20, CMake ≥ 3.20 + Ninja
- SDL2 + OpenGL3 + Dear ImGui (docking)
- Vendored permissive CPU cores per phase (MAME 6502/65816/PPC — BSD-3-Clause;
  Moira, Musashi — MIT). See [THIRDPARTY.md](THIRDPARTY.md) for the license audit.

## Building (Fedora)

```sh
sudo dnf install cmake ninja-build gcc-c++ SDL2-devel mesa-libGL-devel
git clone --recursive <repo-url> && cd applebox
cmake --preset debug
cmake --build --preset debug
ctest --preset debug          # unit tests
./build/debug/applebox        # launch UI
```

## ROMs

Apple ROMs are copyrighted and never distributed here. Place your own dumps
under `roms/<machine>/` — see [roms/README.md](roms/README.md).

## License

MIT (see [LICENSE](LICENSE)). Vendored third-party code retains its original
permissive license and headers; GPL emulators are used strictly as behavioral
references, never as code sources.
