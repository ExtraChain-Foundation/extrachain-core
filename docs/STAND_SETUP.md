# Set up a stand on a chosen device

This procedure needs no previous session, server name, or saved test dataset. Read
the platform and prerequisite sections once. Then read only the section for the
current step. Use [STAND.md](STAND.md) for daily operation and [TESTING.md](TESTING.md)
for required coverage. Commands below run in Bash inside the execution environment.
Keep one shell session for setup, or load the saved `profiles/paths.env` in every new
tool or SSH session after layout creation. Exported variables do not persist between
independent sessions.

## Choose the execution environment

| Device environment | Combined controller and network faults | Other checks |
| --- | --- | --- |
| Linux with systemd | Run locally after the capability probes pass. | Run native build and CTest checks locally. |
| Windows with WSL2 | Run inside a systemd-enabled distribution if all probes pass. Otherwise use a Linux VM on that device. | Keep Windows and WSL results separate. |
| macOS | Use a Linux VM on that device, with the same CPU architecture where possible. | Run macOS build and sanitizer checks on macOS. |
| Linux without systemd, ordinary container, or restricted host | Use a Linux VM that provides the required capabilities, or a separately selected host. | Available native checks remain useful but do not replace the combined stand. |

The current controller is Linux-specific. The fault driver needs `/proc`, user and
network namespaces, and traffic control. Installing Python alone cannot supply these
features on macOS or native Windows. An untested architecture or VM is not automatically
qualified. Build for the execution environment; do not copy incompatible host binaries.
A Linux guest result is a Linux result, not proof of macOS, Windows, or WSL behavior.

For WSL2, use the Linux filesystem for sources, build output, fixtures, and run data.
Enable systemd through the existing distribution configuration if needed. Apply the
documented WSL restart procedure only after other work is stopped. Systemd setup is
described in [Microsoft's WSL configuration reference](https://learn.microsoft.com/en-us/windows/wsl/wsl-config).
Keep the guest and device awake through a continuous run. Disconnecting SSH is safe;
suspending, shutting down, or restarting the execution environment is not equivalent.

No shared server is a default. Use an existing remote host only when the task selects
it. Keep host addresses and credentials in local operator configuration.

## Prerequisites and capability probes

Use Python 3.11 or newer with `venv`, CMake 3.21 or newer, Ninja, Git, a C++23 compiler
and standard library, Bash, GNU coreutils and tar, Zstandard, util-linux (`unshare`),
iproute2 (`ip`, `tc`), `lsof`, and systemd. The calling account needs working `sudo -n`
access for the controller's `systemd-run` and `systemctl stop` operations. Resolve
missing access through the host's normal administrator setup; do not remove the
controller's service isolation or watchdog to avoid that requirement.

The observed Linux reference uses GCC 14.2, CMake 3.31.6, Python 3.13, and msgpack 1.2.2.
These are reference versions, not proof that every other combination works. Package
names differ by distribution. Install missing tools with its package manager. vcpkg
also needs its normal bootstrap tools, including curl, zip, unzip, and pkg-config.
Use a C++23 compile probe if the installed compiler or standard library is uncertain.
The first build needs repository and package access, or complete local dependency
caches. Record a missing dependency or access error instead of repeating the same build.

The preview profile requires 80 GiB free at start and retains a 10 GiB runtime reserve.
Plan additional space for compilation, dependencies, and longer profiles. Start with
two build workers; raise this only after checking available RAM. A 16 GiB RAM guest is
a starting estimate for the preview, not a guaranteed budget for every profile. If a
device lacks resources, choose a larger guest or host instead of weakening the checks.

Run the following once on the chosen Linux environment. Keep its full output in a
small local preflight log. A failed probe blocks stand startup and names the missing
capability; do not start repeated node runs to investigate it.

```bash
set -euo pipefail
export PATH="$PATH:/usr/sbin:/sbin"
test "$(uname -s)" = Linux || { printf 'A Linux execution environment is required\n' >&2; exit 1; }
test "$(cat /proc/1/comm)" = systemd || { printf 'PID 1 must be systemd\n' >&2; exit 1; }
for exc_tool in python3 git cmake ninja cc c++ bash tar zstd unshare ip tc lsof \
                systemctl systemd-run sudo sha256sum curl zip unzip pkg-config; do
    command -v "$exc_tool" >/dev/null || { printf 'Missing tool: %s\n' "$exc_tool" >&2; exit 1; }
done
python3 -c 'import sys; assert sys.version_info >= (3, 11), sys.version'
sudo -n systemd-run --quiet --wait --collect \
    --uid="$(id -u)" --gid="$(id -g)" /usr/bin/true
unshare -Urn bash -euc '
    ip link set lo up
    tc qdisc add dev lo root netem delay 1ms
    tc qdisc del dev lo root
    tc qdisc add dev lo clsact
    tc filter add dev lo egress protocol ip pref 1 flower \
        ip_proto tcp dst_port 12345 action drop
    tc qdisc del dev lo clsact
'
printf 'PASS: service and network-namespace capabilities\n'
```

These network changes exist only in the temporary namespace. If `unshare`, `netem`,
or `flower` fails, retain its error and use a capable environment. Do not change the
host network or disable security controls as a workaround. A short reference probe
does not qualify the full stand on a new platform; the preview must still pass.

## Create an isolated layout

Start in an existing Core checkout at the revision requested for testing. If none
exists, clone `https://github.com/ExtraChain-Foundation/extrachain-core.git` and select
that revision first. Pending edits in a development checkout are not included in HEAD;
prepare a reviewable source snapshot when those edits are the subject of the test.

Choose a new writable root on a local Linux filesystem. The example uses a home
directory, not a machine-specific mount. Do not overwrite an earlier stand root.

```bash
export EXC_REPO="$(git rev-parse --show-toplevel)"
export EXC_CORE_REF="$(git -C "$EXC_REPO" rev-parse HEAD)"
export EXC_STAND_ROOT="$HOME/extrachain-stand-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -m 700 "$EXC_STAND_ROOT"
mkdir -p "$EXC_STAND_ROOT"/{src,build,profiles,logs,runs,fixtures}
export EXC_SOURCE="$EXC_STAND_ROOT/src/extrachain-core"
export EXC_BUILD="$EXC_STAND_ROOT/build/release"
export EXC_SEED="$EXC_STAND_ROOT/fixtures/funded-25000"
export EXC_PYTHON="$EXC_STAND_ROOT/python/bin/python"
git -C "$EXC_REPO" worktree add --detach "$EXC_SOURCE" "$EXC_CORE_REF"
git clone https://github.com/ExtraChain-Foundation/extrachain-3rdparty.git \
    "$EXC_STAND_ROOT/src/extrachain-3rdparty"
git -C "$EXC_STAND_ROOT/src/extrachain-3rdparty" checkout --detach \
    dc9debbcee18c0a2329cf3f85dcab6407c5e1481
python3 -m venv "$EXC_STAND_ROOT/python"
"$EXC_PYTHON" -m pip install 'msgpack==1.2.2' >"$EXC_STAND_ROOT/logs/python-install.log" 2>&1
"$EXC_PYTHON" -m pip freeze >"$EXC_STAND_ROOT/profiles/python-packages.txt"
"$EXC_PYTHON" - <<'PY'
import os, shlex
from pathlib import Path
keys = ('EXC_REPO', 'EXC_CORE_REF', 'EXC_STAND_ROOT', 'EXC_SOURCE',
        'EXC_BUILD', 'EXC_SEED', 'EXC_PYTHON')
path = Path(os.environ['EXC_STAND_ROOT']) / 'profiles/paths.env'
path.write_text(''.join('export ' + key + '=' + shlex.quote(os.environ[key]) + '\n' for key in keys))
print('Setup variables: ' + str(path))
PY
```

The sibling `extrachain-3rdparty` layout is required by CMake for WAMR and bip3x.
The dependency revision above is the reference for this branch. If the source under
test changes that requirement, use its reviewed dependency revision and record it.
Keep the entire source `scripts/` and `tests/` directories; copying only the controller
omits the harness and audit modules.

## Build the native stand tools

Use the repository's `vcpkg.json`. Prefer an existing compatible, recorded dependency
installation. For a fresh installation, use a dedicated vcpkg checkout and record its
exact revision before configuration. The following pin is from the recorded toolchain
checkout; a new platform still requires its own build and preview validation.

```bash
git clone https://github.com/microsoft/vcpkg.git "$EXC_STAND_ROOT/vcpkg"
git -C "$EXC_STAND_ROOT/vcpkg" checkout --detach 58950f88544e4637524dbd6a01d0317cf4cb77fc
export VCPKG_MAX_CONCURRENCY=2
bash "$EXC_STAND_ROOT/vcpkg/bootstrap-vcpkg.sh" -disableMetrics \
    >"$EXC_STAND_ROOT/logs/vcpkg-bootstrap.log" 2>&1
case "$(uname -m)" in
    x86_64) export EXC_TRIPLET=x64-linux ;;
    aarch64|arm64) export EXC_TRIPLET=arm64-linux ;;
    *) printf 'Select and validate a native toolchain for this architecture\n' >&2; exit 1 ;;
esac
export CC="$(command -v cc)"
export CXX="$(command -v c++)"
export SOURCE_DATE_EPOCH="$(git -C "$EXC_SOURCE" show -s --format=%ct HEAD)"
cmake -S "$EXC_SOURCE/tests" -B "$EXC_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_TOOLCHAIN_FILE="$EXC_STAND_ROOT/vcpkg/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET="$EXC_TRIPLET" -DVCPKG_MANIFEST_MODE=ON \
    -DVCPKG_INSTALLED_DIR="$EXC_STAND_ROOT/dependencies" \
    -DPython3_EXECUTABLE="$EXC_PYTHON" \
    -DEXTRACHAIN_BUILD_QT_COMPAT_TESTS=OFF \
    >"$EXC_STAND_ROOT/logs/configure.log" 2>&1
cmake --build "$EXC_BUILD" --parallel 2 --target \
    extrachain-node-run extrachain-shadow-bundle extrachain-dag-audit \
    extrachain-sync-check extrachain-gen-sections \
    >"$EXC_STAND_ROOT/logs/build.log" 2>&1
```

Set `CC` and `CXX` to the chosen compiler paths if `cc` and `c++` are not suitable.
Do this before the configuration command. The tests CMake project creates the stand
executables; configuring only the library root does not create them. CMake also
fetches toolbox 3.4.0 through bip3x unless a local source is supplied. Preserve and
record that dependency. Keep `CMakeCache.txt`, compiler versions, dependency revisions,
and package records as build evidence. vcpkg manifest behavior is documented in
[Microsoft's manifest guide](https://learn.microsoft.com/en-us/vcpkg/consume/manifest-mode).

On build failure, inspect the first relevant error and a bounded log range. Do not
paste the full build log or repeatedly change dependency versions without evidence.
Native macOS or Windows checks need their own compiler, triplet, build directory, and
results. For CTest, build all required targets first, then run
`ctest --test-dir <native-build> --output-on-failure` with output saved to a file.
These five stand targets alone do not constitute a full Release or sanitizer test run.

## Generate and verify the fixture

Generate 24,999 funded Regular transfers. With the initial allocation, the last section
is 25,000. The directory must be new or empty. Never use production accounts or copy
an old failed dataset into a fresh acceptance fixture.

```bash
"$EXC_BUILD/extrachain-gen-sections" 24999 "$EXC_SEED" \
    >"$EXC_STAND_ROOT/logs/fixture.log" 2>&1
"$EXC_PYTHON" - <<'PY'
import json, os
from pathlib import Path
seed = Path(os.environ['EXC_SEED'])
assert int(json.loads((seed / 'dag/range').read_text())['last']) == 25000
print('Fixture range: PASS')
PY
test -z "$(git -C "$EXC_SOURCE" status --porcelain)"
```

Require the generator's successful exit, its zero-rejection result, and the range
check. Preserve its log. The stand performs the network bootstrap and durable audits
again; a range value alone is not proof of correct replication.

## Generate a profile for these paths

The script below fills the checked-in example. It also pins `extrachain-sync-check`,
which the network bootstrap invokes, Python package files, and dynamically loaded
libraries. The explicit PATH makes shell-invoked `python3` use the same environment.
Do not rely on activation of a virtual environment surviving a systemd launch.

```bash
"$EXC_PYTHON" - <<'PY'
import hashlib, json, os, subprocess
from pathlib import Path
import msgpack
root = Path(os.environ['EXC_STAND_ROOT'])
source = Path(os.environ['EXC_SOURCE'])
build = Path(os.environ['EXC_BUILD'])
python = os.environ['EXC_PYTHON']
paths = {'/stand/source': str(source), '/stand/build': str(build),
         '/stand/seed': os.environ['EXC_SEED'], '/stand/python/bin/python': python,
         '/stand/runs': str(root / 'runs')}
def replace(value):
    if isinstance(value, dict): return {k: replace(v) for k, v in value.items()}
    if isinstance(value, list): return [replace(v) for v in value]
    if isinstance(value, str):
        for old, new in paths.items(): value = value.replace(old, new)
    return value
profile = replace(json.loads((source / 'docs/stand.example.json').read_text()))
with (build / 'extrachain-node-run').open('rb') as stream:
    profile['stages'][0]['expect']['binary_sha256'] = hashlib.file_digest(stream, 'sha256').hexdigest()
profile['environment'] = {'PATH': str(root / 'python/bin') + ':/usr/sbin:/usr/bin:/sbin:/bin'}
inputs = {item['path'] for item in profile['inputs']}
inputs.update(str(build / name) for name in ('extrachain-sync-check', 'extrachain-gen-sections'))
inputs.update(str(p) for p in Path(msgpack.__file__).parent.rglob('*')
              if p.is_file() and '__pycache__' not in p.parts and p.suffix != '.pyc')
inputs.update(str(root / name) for name in ('python/pyvenv.cfg', 'profiles/python-packages.txt'))
for binary in [python] + [str(build / name) for name in
        ('extrachain-node-run', 'extrachain-shadow-bundle', 'extrachain-dag-audit', 'extrachain-sync-check')]:
    result = subprocess.run(['ldd', binary], text=True, capture_output=True,
                            env=dict(os.environ, LC_ALL='C'))
    output = result.stdout + result.stderr
    if result.returncode and not any(text in output for text in
            ('not a dynamic executable', 'statically linked')):
        raise SystemExit('Cannot inspect runtime libraries: ' + binary)
    if 'not found' in output: raise SystemExit('Missing runtime library: ' + binary)
    for line in output.splitlines():
        fields = line.split()
        candidate = fields[2] if len(fields) > 2 and fields[1] == '=>' else fields[0] if fields else ''
        if candidate.startswith('/'): inputs.add(candidate)
profile['inputs'] = [{'path': p} for p in sorted(inputs)]
with (root / 'profiles/preview.json').open('x') as stream:
    json.dump(profile, stream, indent=2)
print('Preview profile ready')
PY
"$EXC_PYTHON" "$EXC_SOURCE/scripts/stand.py" prepare \
    "$EXC_STAND_ROOT/profiles/preview.json" --output "$EXC_STAND_ROOT/profiles/preview-frozen.json"
"$EXC_PYTHON" "$EXC_SOURCE/scripts/stand.py" start \
    "$EXC_STAND_ROOT/profiles/preview-frozen.json" >"$EXC_STAND_ROOT/profiles/launch.json"
cat "$EXC_STAND_ROOT/profiles/launch.json"
```

Run this only on trusted binaries built from the selected source. The profile creates
a 600-second minimum workload with four 150-second waves, six senders, and 48 transfers
per sender. Expect 1,152 receipts, 224 file copies, and eight stopped-data audits. Setup,
recovery, and final audits add time. Read the final controller result before claiming
success. Keep the source, runtime inputs, and controller unchanged until it finishes.

## Operate with bounded output

Use the Python and source paths above and the `directory` from `launch.json`:

| Need | Command suffix for `stand.py` | Output policy |
| --- | --- | --- |
| User asks for progress | `status <run-directory>` | One compact result. |
| Run completes | `report <run-directory>` | One final report; retain the run ID and manifest hash. |
| Run fails | `diagnose <run-directory>` | Initial evidence below 20 KiB. |
| A specific error needs context | `diagnose <run-directory> --file <relative-path> --offset <byte-offset> --limit 8192` | Read only the missing range; maximum 16 KiB per request. |
| User requests a stop | `stop <run-directory>` | Record interruption and clean up the service. |
| Retire an inactive run | `archive <run-directory>` | Verify the archive before any source removal. |

Keep full logs on the execution device. Check events locally through existing job
automation; do not keep a model in a status polling loop or repeatedly scan all node
logs. Record a small handoff file outside the source checkout: host/guest identity,
source and dependency revisions, build path, Python path, manifest path/hash, run ID,
service name, report path, and the next unresolved action. Include no credentials.
On resumption, read that file and one status instead of reconstructing old sessions.

Choose checks by scope. Reuse a successful command stage only through the controller's
exact-input checks. Run a focused reproduction after a fix, then the required combined
profile. Broaden tests only for changed behavior, a failure, or an unresolved concern.
Do not rerun a complete suite for a documentation-only change. Full acceptance still
requires every prerequisite and the continuous long and final stages in [STAND.md](STAND.md).
The preview is not an acceptance-profile generator. Additional stage commands and
platform results must be configured explicitly; never label a preview as acceptance.
