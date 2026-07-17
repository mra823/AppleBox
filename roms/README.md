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

Each machine's required files, sizes, and SHA-256 hashes are declared in its
catalog entry (see `src/core/machine.cpp`). A machine only becomes selectable
in the configuration dialog once its required ROM set validates.
