#!/usr/bin/env bash
set -euo pipefail

python_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
root_dir="$(cd "$python_dir/.." && pwd)"
wamr_dir="$(cd "$root_dir/../extrachain-3rdparty/WAMR" && pwd)"
build_dir="${EXCO_PYTHON_TEST_BUILD_DIR:-/tmp/exco-python-wamr}"

cmake -S "$wamr_dir/product-mini/platforms/linux" -B "$build_dir/wamr" \
    -DCMAKE_BUILD_TYPE=Release \
    -DWAMR_BUILD_AOT=0 \
    -DWAMR_BUILD_BULK_MEMORY=0 \
    -DWAMR_BUILD_EXCE_HANDLING=1 \
    -DWAMR_BUILD_FAST_INTERP=0 \
    -DWAMR_BUILD_INSTRUCTION_METERING=1 \
    -DWAMR_BUILD_INTERP=1 \
    -DWAMR_BUILD_LIBC_BUILTIN=0 \
    -DWAMR_BUILD_LIBC_WASI=0 \
    -DWAMR_BUILD_REF_TYPES=0 \
    -DWAMR_BUILD_SIMD=0
cmake --build "$build_dir/wamr" --parallel
cc -O2 \
    -I"$wamr_dir/core/iwasm/include" \
    "$python_dir/tests/wamr_runner.c" \
    "$build_dir/wamr/libiwasm.a" \
    -Wl,-z,noexecstack \
    -lpthread -lm -ldl \
    -o "$build_dir/wamr_runner"
python3 "$python_dir/tests/run-linux-poc.py" "$build_dir/wamr_runner"
