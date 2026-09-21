# Autonomous validation controller

`scripts/stand.py` runs a declared sequence of checks on a Linux host with systemd.
The controller keeps full logs on that host. An operator reads the compact status or
final report. No remote session or repeated model request is needed to keep a run alive.

The controller does not change the node workload, network faults, five-second resource
sampling, durable receipts, or stopped-data audits in `shadow_endurance.py`.

## Prepare and run

1. Use a clean source checkout and a separate run root. Store profiles and frozen
   manifests outside that checkout, in an existing directory such as `/stand/profiles`.
   Set the absolute paths in a copy of [stand.example.json](stand.example.json).
   Use the Python environment with the harness dependencies. List every external
   executable, fixture, configuration,
   and dependency that can affect the result in `inputs`. Source-controlled scripts
   are covered by the source revision and tree. Do not change inputs during a run.
2. Set `expect.binary_sha256` to the SHA-256 of `extrachain-node-run`. Set the disk
   budget for the complete sequence. The runtime reserve defaults to 10 GiB.
3. Freeze the manifest and start the service:

```sh
python scripts/stand.py prepare /stand/profiles/profile.json --output /stand/profiles/frozen.json
python scripts/stand.py start /stand/profiles/frozen.json
```

`start` returns the run directory, service name, events path, and report path. The
service runs as the calling user. Starting and stopping the system service requires
the host's existing `sudo -n` access. The controller does not change login persistence
or install a daemon. Each run uses a new directory and a separate service control group.

```sh
python scripts/stand.py status /absolute/run-directory
python scripts/stand.py stop /absolute/run-directory
python scripts/stand.py report /absolute/run-directory
python scripts/stand.py diagnose /absolute/run-directory
python scripts/stand.py diagnose /absolute/run-directory --file combined-short/output.log --offset 0 --limit 8192
```

Use `status` for a user request or an operational decision. Do not poll it through a
model to wait for completion. A scheduler can read `events.jsonl` and the final result
locally. This controller does not claim to send a message or wake a model automatically.
Full logs remain available for a targeted investigation. Diagnostic reads accept at
most 16 KiB. The initial diagnostic document is below 20 KiB.

## Sequence and acceptance

A manifest contains ordered `stages`, `required_stages`, and `purpose`. Every listed
stage must pass; the first failure stops the sequence. Commands are argument arrays.
`{run}` and `{stage}` expand to the new directories in command arguments, stage
environment values, and the working directory. The default environment is explicit;
ambient workload variables are not inherited.

Two stage types are supported:

- `command`: a deadline, a command, and explicit result checks. A check reads a text
  pattern or compares top-level JSON fields through `equals`. Exit code zero alone
  cannot pass a stage. Keep checks specific to the test's final result.
- `endurance`: runs the existing combined harness and checks its configuration,
  continuous duration, every wave, receipts, and file-copy counts. It also requires
  the shutdown checkpoint, complete durable receipts and files, and all eight
  stopped-data audit results. Evidence files are hashed.

Use `purpose: validation` for controller tests, a reproduction, or a short run. Such
a PASS does not establish full acceptance. `purpose: acceptance` requires these
stages: `preview`, `extended`, `backoff`, `large`, `legacy`, `history`, `console`,
`release-linux`, `release-macos`, `sanitizers-macos`, `endurance`, and `final-short`.
Configure the existing prerequisite commands with their result checks and pinned
inputs. Platform checks must execute on their declared platform; a Linux result
cannot replace a macOS or WSL result.

The last two stages must be typed endurance stages. The long stage must run for
21,600 seconds with 300-second waves, six senders, and 48 transfers per sender. The
final short stage must last at least 600 seconds. Stopped-data audits are part of
each endurance stage, before its PASS. Long and final stages cannot reuse cached
results. Interrupted durations are never added together.

For an explicitly cacheable command stage, `reuse_from` can name a previous stage
directory. Reuse requires a completed successful service, matching source, platform,
controller, inputs, environment, and stage configuration, plus unchanged evidence
hashes. A mismatch stops the run. Remove `reuse_from` to execute again. Reused stages
point to the original evidence; they cannot become the source of another cache chain.
Preserve the original directory while its evidence is referenced.

## Results and recovery

| State | Meaning |
| --- | --- |
| `PASS` | Every stage passed, inputs stayed fixed, and systemd completed service cleanup. |
| `FAIL` | A test command failed. |
| `INTERRUPTED` | The operator requested a stop through the controller. |
| `INFRA_ERROR` | Missing or invalid evidence, changed inputs, disk limit, deadline, controller failure, or watchdog failure. |

`QUEUED`, `PREFLIGHT`, `RUNNING`, and `FINALIZING` are incomplete states. A stale
heartbeat is marked as stale and never converted to PASS. The 30-second systemd
watchdog detects a stalled controller. A stage deadline detects a stalled test.
Systemd terminates the complete process control group, including children that create
their own process session. `ExecStopPost` records abnormal termination and the service
result. If the host loses power before this record can be written, the run remains
incomplete. Investigate it; do not resume it as continuous acceptance.

Each completed run contains `manifest.json`, `status.json`, `events.jsonl`, per-stage
`result.json`, logs, and evidence hashes. `report.md`, `tracker-update.md`, and
`pr-update.md` are generated from the results. The last two files are drafts; the
controller does not publish them. Failed runs also have `diagnostic.json` with the
failure reason, artifact paths, sizes, and bounded excerpts. Each stage records its
first matching error and up to eight groups of repeated errors in `errors.json`.
These groups are diagnostic hints from stage output; they do not decide the result.

## Retention

```sh
python scripts/stand.py archive /absolute/run-directory --remove-source
```

Archiving requires a terminal result and a stopped service. It creates a sparse-aware
Zstandard archive, checks compression integrity, compares every archived file against
the source, and records the archive hash. Only then can `--remove-source` remove the
directory. Keep failure evidence and the latest acceptance artifacts. Archive or
remove obsolete successful datasets according to an explicit retention decision.
Do not remove a cached source while another manifest still depends on it.

## Controller tests

```sh
python -m unittest discover -s scripts -p test_stand.py -v
EXTRACHAIN_STAND_SYSTEM_TEST_ROOT=/absolute/test-root \
EXTRACHAIN_STAND_TEST_SOURCE=/absolute/clean-source \
python -m unittest discover -s scripts -p test_stand_systemd.py -v
```

The second command is opt-in. It creates short transient services, injects failures,
and checks descendant cleanup, stop classification, watchdog behavior, disk and time
limits, and exact-input cache reuse. It keeps the small test evidence directories.
Run a combined DAG + DFS preview after a controller change. Saved event replay is a
diagnostic check only; it cannot establish a new acceptance result.
