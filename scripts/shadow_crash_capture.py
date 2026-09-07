#!/usr/bin/env python3
"""Run an offline verifier under GDB and keep a core file on a fatal signal.

Preserve normal address randomization and the verifier's exit status. This wrapper
is only for offline checks, after the workload processes have stopped.
"""
import argparse
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == '--':
        command = command[1:]
    if not command:
        parser.error('Supply the verifier command after --')
    debugger = shutil.which('gdb')
    if debugger is None:
        parser.error('Crash capture requires gdb')
    core = args.core.resolve()
    if any(character in str(core) for character in ('\n', '\r')):
        parser.error('The core path must not contain a line break')
    script = core.with_suffix(core.suffix + '.gdb')
    if core.exists() or script.exists():
        parser.error('Use a new core path to preserve existing evidence')
    with script.open('x') as output:
        output.write(
            'set pagination off\n'
            'set confirm off\n'
            'set debuginfod enabled off\n'
            'set disable-randomization off\n'
            'catch signal SIGSEGV SIGABRT SIGBUS SIGILL SIGFPE\n'
            'commands\n'
            'silent\n'
            'printf "OFFLINE VERIFIER CRASH\\n"\n'
            'thread apply all bt 25\n'
            'info registers\n'
            f'generate-core-file {core}\n'
            'quit 1\n'
            'end\n'
            'run\n'
            'quit $_exitcode\n')
    return subprocess.run([debugger, '-q', '-batch', '-x', str(script), '--args', *command]).returncode


if __name__ == '__main__':
    raise SystemExit(main())
