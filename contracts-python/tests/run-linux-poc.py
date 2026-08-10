#!/usr/bin/env python3
import importlib.util
import json
import os
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "contracts/standard/micropython_runtime.wasm"
RUNNER = Path(sys.argv[1])
ALICE = "1" * 40
BOB = "2" * 40
POC_LIMIT = 20_000_000
PRODUCTION_LIMIT = 5_000_000

spec = importlib.util.spec_from_file_location("exco_sdk", ROOT / "contracts-python/sdk/extrachain.py")
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)


def unsigned_leb(source, offset):
    value = 0
    shift = 0
    while True:
        current = source[offset]
        offset += 1
        value |= (current & 0x7F) << shift
        if current < 0x80:
            return value, offset
        shift += 7


def bytecode(path):
    source = path.read_bytes()
    offset = 8
    while offset < len(source):
        section_id = source[offset]
        offset += 1
        length, offset = unsigned_leb(source, offset)
        section = source[offset:offset + length]
        offset += length
        if section_id != 0:
            continue
        name_length, position = unsigned_leb(section, 0)
        name = section[position:position + name_length].decode()
        if name == "extrachain.python.bytecode":
            return section[position + name_length:]
    raise RuntimeError("Python bytecode section is missing")


def invoke(artifact, method, arguments, state=b"", limit=POC_LIMIT):
    code = bytecode(artifact)
    request = sdk.encode([
        [ALICE, ALICE, "test-contract", 1, 0],
        method,
        sdk.encode(arguments),
        state,
        [[], []],
        4,
    ])
    envelope = len(code).to_bytes(4, "big") + code + request
    with tempfile.TemporaryDirectory(prefix="exco-python-flow-") as temporary:
        input_path = Path(temporary) / "input.bin"
        output_path = Path(temporary) / "output.bin"
        input_path.write_bytes(envelope)
        completed = subprocess.run(
            [str(RUNNER), str(RUNTIME), str(input_path), str(output_path), str(limit)],
            capture_output=True,
            text=True,
            check=False,
        )
        if completed.returncode:
            return [False, state, b"", [], [], completed.stderr.strip()]
        return sdk.decode(output_path.read_bytes())


counter = ROOT / "contracts-python/fixtures/counter.wasm"
token = ROOT / "contracts-python/fixtures/token_x.wasm"

first_counter = invoke(counter, "init", [7])
second_counter = invoke(counter, "init", [7])
assert first_counter[0] and first_counter == second_counter
incremented = invoke(counter, "increment", [5], first_counter[1])
assert incremented[0]
queried = invoke(counter, "value", [], incremented[1])
assert queried[0] and queried[1] == incremented[1] and sdk.decode(queried[2]) == 12
def token_flow():
    initialized = invoke(token, "init", ["Token X", "X", 0, 1000, []])
    assert initialized[0]
    migrated = invoke(token, "migrate", [], initialized[1])
    assert migrated[0]
    frozen = invoke(token, "transfer", [BOB, 999], migrated[1])
    assert frozen[0]
    denied = invoke(token, "transfer", [BOB, 1], frozen[1])
    assert not denied[0] and denied[1] == frozen[1]
    return migrated, frozen


token_migrated, token_state = token_flow()
limited_transfer = invoke(token, "transfer", [BOB, 999], token_migrated[1], PRODUCTION_LIMIT)
assert not limited_transfer[0]
samples = []
for _ in range(int(os.environ.get("EXCO_PYTHON_BENCHMARK_SAMPLES", "25"))):
    started = time.perf_counter_ns()
    token_flow()
    samples.append(time.perf_counter_ns() - started)
samples.sort()
result = {
    "counter_state_bytes": len(incremented[1]),
    "token_state_bytes": len(token_state[1]),
    "production_instruction_limit": PRODUCTION_LIMIT,
    "minimum_verified_poc_limit": POC_LIMIT,
    "samples": len(samples),
    "process_isolated_mean_ns": int(statistics.mean(samples)),
    "process_isolated_p50_ns": samples[len(samples) // 2],
    "process_isolated_p95_ns": samples[min(len(samples) - 1, len(samples) * 95 // 100)],
}
print(json.dumps(result, sort_keys=True))
