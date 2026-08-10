#!/usr/bin/env bash
set -euo pipefail

runtime_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python_dir="$(cd "$runtime_dir/.." && pwd)"
micropython_dir="${MICROPYTHON_DIR:-/mnt/e/dev/Builds/exco-micropython-v128}"
build_dir="${EXCO_PYTHON_BUILD_DIR:-$python_dir/build/runtime}"

test "$(git -C "$micropython_dir" rev-parse HEAD)" = "e0e9fbb17ed6fd06bb76e266ae554784c9c80804"
make -C "$runtime_dir" -f build-embed.mk \
    MICROPYTHON_TOP="$micropython_dir" \
    BUILD="$build_dir/embed" \
    PACKAGE_DIR="$build_dir/micropython_embed"

mapfile -t sources < <(find "$build_dir/micropython_embed/py" -maxdepth 1 -name '*.c' -print | sort)

emcc "${sources[@]}" "$runtime_dir/exco_runtime.c" \
    -I"$runtime_dir" \
    -I"$build_dir/micropython_embed" \
    -I"$build_dir/micropython_embed/genhdr" \
    -Oz -DNDEBUG -std=c99 \
    -s STANDALONE_WASM=1 \
    -s SUPPORT_LONGJMP=wasm \
    -s LEGALIZE_JS_FFI=0 \
    -s ALLOW_MEMORY_GROWTH=0 \
    -s INITIAL_MEMORY=8388608 \
    -s TOTAL_STACK=262144 \
    -s FILESYSTEM=0 \
    -s MALLOC=emmalloc \
    -s EXPORTED_FUNCTIONS='["_exc_invoke","_exc_result_len"]' \
    --no-entry \
    -o "$build_dir/micropython_runtime.wasm"

wasm-dis --enable-exception-handling "$build_dir/micropython_runtime.wasm" \
    -o "$build_dir/micropython_runtime.wat"
node "$runtime_dir/internalize-temp-ret.mjs" \
    "$build_dir/micropython_runtime.wat" \
    "$build_dir/micropython_runtime.internal.wat"
wasm-as --enable-exception-handling "$build_dir/micropython_runtime.internal.wat" \
    -o "$build_dir/micropython_runtime.internal.wasm"
wasm-opt --enable-exception-handling "$build_dir/micropython_runtime.internal.wasm" -Oz --strip-debug \
    -o "$build_dir/micropython_runtime.optimized.wasm"
cp "$build_dir/micropython_runtime.optimized.wasm" "$python_dir/../contracts/standard/micropython_runtime.wasm"
