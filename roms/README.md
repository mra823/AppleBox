# AppleBox ROMs

Apple ROMs are copyrighted by Apple and are **not** distributed with AppleBox.
Supply your own dumps here, one subfolder per machine (86Box-style):

```
roms/
  apple1/        # Woz Monitor ($FF00), optional Apple 1 BASIC ($E000)
  apple2plus/
  apple2ee/
  apple2gs/
  macplus/
  ...
```

## Apple I (Phase 1 — implemented)

| File | Size | Location | Required |
|------|------|----------|----------|
| `apple1/wozmon.rom` | 256 B | $FF00–$FFFF | yes |
| `apple1/basic.rom` | 4096 B | $E000–$EFFF | no |

The Woz Monitor image is the standard 256-byte dump of the Apple-1 monitor
PROMs. With it in place, `test_apple1` also runs the Phase 1 acceptance test
(boot to the `\` prompt, enter and run a hex program).

Each machine's required files, sizes, and SHA-256 hashes are declared in its
catalog entry (see `src/core/machine.cpp`). A machine only becomes selectable
in the configuration dialog once its required ROM set validates.
