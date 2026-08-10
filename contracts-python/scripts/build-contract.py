#!/usr/bin/env python3
import argparse
import ast
import subprocess
import tempfile
from pathlib import Path


SDK_VERSION = "0.1.0-poc"
MAX_MODULE_BYTES = 2 * 1024 * 1024


def validate_source(source):
    tree = ast.parse(source)
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            raise ValueError("Python contracts cannot import modules")
        if isinstance(node, ast.ImportFrom):
            if node.module != "extrachain":
                raise ValueError("Python contracts can import only the ExtraChain SDK")
            if node.col_offset != 0 or node.end_lineno != node.lineno:
                raise ValueError("The ExtraChain SDK import must be one top-level line")
        if isinstance(node, ast.Constant) and isinstance(node.value, (float, complex)):
            raise ValueError("Python contracts cannot use floating-point or complex values")
        if isinstance(node, (ast.AsyncFunctionDef, ast.AsyncFor, ast.AsyncWith, ast.Await)):
            raise ValueError("Python contracts cannot use asynchronous syntax")
        if isinstance(node, (ast.Set, ast.SetComp)):
            raise ValueError("Python contracts cannot use native sets; use StateSet")


def unsigned_leb(value):
    result = bytearray()
    while True:
        current = value & 0x7F
        value >>= 7
        result.append(current | (0x80 if value else 0))
        if not value:
            return bytes(result)


def custom_section(name, value):
    encoded_name = name.encode("utf-8")
    payload = unsigned_leb(len(encoded_name)) + encoded_name + value
    return b"\x00" + unsigned_leb(len(payload)) + payload


def runtime_hash(path):
    if path.stat().st_size > MAX_MODULE_BYTES:
        raise ValueError("MicroPython runtime exceeds the contract module size limit")
    output = subprocess.check_output(["b3sum", "--no-names", str(path)], text=True)
    value = output.strip()
    if len(value) != 64 or any(character not in "0123456789abcdef" for character in value):
        raise ValueError("MicroPython runtime has an invalid BLAKE3 hash")
    return value


def bundled_source(sdk, contract):
    lines = []
    for line in contract.splitlines():
        if line.startswith("from extrachain import "):
            continue
        lines.append(line)
    return sdk.rstrip() + "\n\n" + "\n".join(lines).lstrip()


def main():
    parser = argparse.ArgumentParser(description="Build an ExtraChain MicroPython contract artifact")
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--mpy-cross", type=Path, required=True)
    arguments = parser.parse_args()

    source = arguments.source.read_text(encoding="utf-8")
    validate_source(source)
    combined = bundled_source(arguments.sdk.read_text(encoding="utf-8"), source)
    with tempfile.TemporaryDirectory(prefix="exco-python-") as temporary:
        input_path = Path(temporary) / "contract.py"
        bytecode_path = Path(temporary) / "contract.mpy"
        input_path.write_text(combined, encoding="utf-8", newline="\n")
        subprocess.run(
            [str(arguments.mpy_cross), "-s", "contract.py", "-o", str(bytecode_path), str(input_path)],
            check=True,
        )
        bytecode = bytecode_path.read_bytes()

    artifact = b"\x00asm\x01\x00\x00\x00"
    artifact += custom_section("extrachain.language", b"python")
    artifact += custom_section("extrachain.python.runtime", runtime_hash(arguments.runtime).encode("ascii"))
    artifact += custom_section("extrachain.python.sdk", SDK_VERSION.encode("ascii"))
    artifact += custom_section("extrachain.python.bytecode", bytecode)
    artifact += custom_section("extrachain.python.source", source.encode("utf-8"))
    if len(artifact) > MAX_MODULE_BYTES:
        raise ValueError("Python contract artifact exceeds the module size limit")
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_bytes(artifact)


if __name__ == "__main__":
    main()
