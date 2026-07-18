#!/usr/bin/env bash
# AppleBox — fetch CPU test data (never committed; see .gitignore).
#   tests/data/harte/6502/v1/   Tom Harte SingleStepTests (6502, 10k/opcode)
#   tests/data/klaus/           Klaus Dormann functional + interrupt binaries
# SPDX-License-Identifier: MIT
set -euo pipefail
cd "$(dirname "$0")/.."

DATA=tests/data
mkdir -p "$DATA"

# --- Tom Harte SingleStepTests (6502) --------------------------------------
if [ ! -d "$DATA/harte/6502/v1" ]; then
    echo "Fetching SingleStepTests 6502 (sparse clone, ~600 MB)..."
    tmp=$(mktemp -d)
    git clone --depth 1 --filter=blob:none --sparse \
        https://github.com/SingleStepTests/65x02.git "$tmp/65x02"
    git -C "$tmp/65x02" sparse-checkout set 6502/v1
    mkdir -p "$DATA/harte/6502"
    mv "$tmp/65x02/6502/v1" "$DATA/harte/6502/v1"
    rm -rf "$tmp"
else
    echo "Harte 6502 data already present."
fi

# --- Klaus Dormann test binaries (pinned commit) ----------------------------
# Note: only the functional test ships prebuilt. 6502_interrupt_test.a65 has
# no bin_files build; assemble it yourself (as65) and drop
# 6502_interrupt_test.bin into tests/data/klaus/ to enable that test.
KLAUS_COMMIT=7954e2dbb49c469ea286070bf46cdd71aeb29e4b
KLAUS_BASE="https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/$KLAUS_COMMIT/bin_files"
mkdir -p "$DATA/klaus"
for f in 6502_functional_test.bin 6502_functional_test.lst; do
    if [ ! -f "$DATA/klaus/$f" ]; then
        echo "Fetching $f..."
        curl -sfL "$KLAUS_BASE/$f" -o "$DATA/klaus/$f"
    fi
done

echo "Done. Test data in $DATA/"
