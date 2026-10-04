#!/bin/bash
# Usage: ./run.sh <input.c>
# Builds the compiler if needed, generates code.asm + optimized_code.asm,
# assembles both with FASM, and runs both resulting executables.
set -e
cd "$(dirname "$0")"

if [ -z "$1" ]; then
    echo "Usage: $0 <input.c>"
    exit 1
fi

if [ ! -x ./icg.out ]; then
    ./build.sh
fi

echo "[1/3] Generating assembly for: $1"
LD_LIBRARY_PATH=/usr/local/lib ./icg.out "$1"

FASM_BIN=$(command -v fasm || true)
if [ -z "$FASM_BIN" ]; then FASM_BIN=./tools/fasm; fi

echo "[2/3] Assembling with FASM ..."
"$FASM_BIN" code.asm code.out
chmod +x code.out
"$FASM_BIN" optimized_code.asm optimized_code.out
chmod +x optimized_code.out

echo "[3/3] Running:"
echo "---- code.out (unoptimized) ----"
./code.out
echo "---- optimized_code.out ----"
./optimized_code.out
