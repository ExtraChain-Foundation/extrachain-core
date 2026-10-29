#!/usr/bin/env python3
"""Build the pinned Linux contract toolchain packages for a test release."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tools', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    tools, output = args.tools.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    node = tools / 'node-v24.10.0-linux-x64/bin/node'
    environment = dict(os.environ, PATH=f'{node.parent}:{tools / "cargo/bin"}:' + os.environ['PATH'],
                       CARGO_HOME=str(tools / 'cargo'), RUSTUP_HOME=str(tools / 'rustup'))
    subprocess.run([str(node), '--version'], env=environment, check=True)
    subprocess.run(['npm', 'ci', '--ignore-scripts'], cwd=root / 'contracts-as', env=environment, check=True)
    subprocess.run([str(node), str(root / 'contracts-as/scripts/create-toolchain-package.mjs'),
                    '--node', str(node), '--output', str(output / 'assemblyscript-0.3.0.tar.xz')],
                   env=environment, check=True)
    subprocess.run(['cargo', 'fetch', '--manifest-path', str(root / 'contracts-rs/Cargo.toml')],
                   env=environment, check=True)
    staging = output / 'rust-package'
    staging.mkdir(exist_ok=False)
    (staging / 'bin').mkdir()
    for name in ('cargo', 'rustc', 'rustup'):
        shutil.copy2(tools / 'cargo/bin' / name, staging / 'bin' / name, follow_symlinks=True)
    shutil.copytree(tools / 'rustup', staging / 'rustup')
    shutil.copytree(tools / 'cargo/registry', staging / 'cargo-home/registry')
    for name in ('sdk', 'macros', 'components', 'catalog', 'templates'):
        shutil.copytree(root / 'contracts-rs' / name, staging / name)
    (staging / 'Cargo.toml').write_text(
        '[workspace]\nmembers = ["sdk", "macros", "components"]\nresolver = "3"\n'
        '[workspace.package]\nedition = "2024"\nlicense = "LGPL-3.0-or-later"\nrust-version = "1.97"\n')
    with tarfile.open(output / 'rust-0.3.0.tar.xz', 'w:xz', dereference=True, preset=1) as archive:
        for child in sorted(staging.iterdir()):
            archive.add(child, arcname=child.name)
    result = dict(status='PASS', node_version=subprocess.check_output([str(node), '--version'], text=True).strip(),
                  rust_version=subprocess.check_output(['rustc', '--version'], env=environment, text=True).strip(),
                  packages=['assemblyscript-0.3.0.tar.xz', 'rust-0.3.0.tar.xz'])
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
