# ExtraChain MicroPython Contract PoC

This directory contains a research proof of concept. It does not add Python as a production contract language.

The PoC uses MicroPython 1.28.0 at the commit in `VERSION.lock`. It creates an integer-only runtime without imports, files, network access, a clock, random data, threads, a REPL, or the source compiler. A contract artifact contains persistent MicroPython bytecode and the metadata that binds it to the embedded runtime.

Python execution is disabled by default. A test must set `RuntimeTuning.enable_python_poc` to `true`. Core then accepts the PoC only on Linux x64. Windows and Android return `RuntimeUnavailable`.

## Build

Build `mpy-cross` in the pinned MicroPython checkout. Then run:

```bash
contracts-python/runtime/build-runtime.sh
contracts-python/build-examples.sh
```

The scripts expect the checkout at `/mnt/e/dev/Builds/exco-micropython-v128`. Set `MICROPYTHON_DIR` to use another location. The runtime script rejects a checkout at another commit.

The `fixtures/` directory contains the verified counter and Token X artifacts that the Core test uses. Rebuild these files after a runtime, SDK, or example change and compare their hashes before commit.

## Test

```bash
EXCO_PYTHON_BENCHMARK_SAMPLES=25 contracts-python/tests/run-linux-poc.sh
```

The Linux test runs the counter and Token X flows through WAMR. It also checks that the current production instruction limit rejects the Token X flow. See the ExCo knowledge vault for the result and recommendation.

## Artifact sections

- `extrachain.language=python`
- `extrachain.python.runtime`
- `extrachain.python.sdk`
- `extrachain.python.bytecode`
- `extrachain.python.source`

The source section is public metadata. Do not put a secret in contract source or call data.
