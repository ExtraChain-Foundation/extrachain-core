#!/usr/bin/env bash
set -euo pipefail

python_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
micropython_dir="${MICROPYTHON_DIR:-/mnt/e/dev/Builds/exco-micropython-v128}"
builder="$python_dir/scripts/build-contract.py"
runtime="$python_dir/../contracts/standard/micropython_runtime.wasm"
sdk="$python_dir/sdk/extrachain.py"
compiler="$micropython_dir/mpy-cross/build/mpy-cross"

for example in counter token_x; do
    python3 "$builder" \
        "$python_dir/examples/$example.py" \
        "$python_dir/build/$example.wasm" \
        --runtime "$runtime" \
        --sdk "$sdk" \
        --mpy-cross "$compiler"
done
