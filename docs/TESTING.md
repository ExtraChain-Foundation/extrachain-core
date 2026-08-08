# Testing methodology — DAG consensus & DFS replication

How the node is stress-tested against a live network of real nodes, and the traps that
make a test harness lie to you. Written from the 2026-08 sessions that found seven
classes of consensus and integrity bugs, none of which were visible by reading the code.

Stands live in a separate worktree, never in a working repository.

> **Rule: any change to the core is validated on a combined DAG + DFS stand — both at
> once, not one at a time.** Every bug in the 2026-08 sessions came from the interaction:
> bulk file traffic delayed consensus messages until transactions fell out of the
> acceptance window, and a replication relay corrupted files while the chain looked
> perfectly healthy. A DAG-only run and a DFS-only run were both green on builds where
> the combined run diverged within minutes. If a change touches the network layer,
> storage, or the chain, it is not tested until it has survived a combined run.

---

## 0. Principles

1. **Test the runtime, not units.** Start real nodes (the headless console binary built
   against the core under test), push real traffic through real sockets, then read the
   consequences off disk and out of the logs. The proof is observed state.
2. **One invariant per stand.** DFS: *nothing is lost, stuck or looping*. DAG: *every
   node holds the same set of transactions in every section*. Every check reduces to that
   invariant on physical artifacts — files on disk, section files, control hashes — never
   on UI or counters.
3. **Deterministic chaos.** All randomness (sizes, order, kills) comes from a seeded
   `random.Random(SEED)` so a failure reproduces. A/B comparisons are only meaningful on
   the *same* seed: consensus splits are extremely timing-sensitive.
4. **Do not bypass the network layer.** Several nodes on one host get separate loopback
   addresses, not just separate ports — otherwise connection dedup, discovery and
   `ShareConnections` never run for real, and that is exactly where bugs hide.
5. **Stand-only core changes are marked `TEST STAND ONLY (do not commit)`.** Product
   fixes go in as their own commits; the env hacks never do.

---

## 1. Stand-only core hacks

| Env | Purpose |
|-----|---------|
| `EXC_BIND_IP` | bind the listener to a specific loopback alias — many nodes, one port |
| `EXC_WS_PORT` / `EXC_DIAL_PORT` | port overrides for star topologies |
| `EXC_NO_REDIAL` | the seed node never dials out |
| `EXC_NO_TOO_OFTEN` | lifts the production 4.5s-per-sender transaction limit so a stand can flood |

Console client (also a stand copy): `--join` creates a local user profile to join an
existing network; stdin input had to be implemented for UNIX (the stock `startInput()` is
empty under `Q_OS_UNIX`, so headless command control does not work at all on macOS);
`reward self N` sends a Reward transaction, which needs no balance for `amount <= 3` —
Regular transactions are unusable for flooding because nothing can hand out a balance.

Loopback aliases, once per boot:

```
for i in 2 3 4 5 6 7; do sudo ifconfig lo0 alias 127.0.0.$i up; done
```

---

## 2. Driving a node from the harness

- **Start:** `Popen([BIN, "--current-dir", ".", "--login", L, "--password", P,
  "--debug-logs"], cwd=nodedir, env=..., stdin=fd, stdout=logf)`. First node of a network:
  `--core --dag-genesis`; the rest: `--join` plus a `.settings` with `first_node`.
- **Commands:** through a named FIFO opened with `os.open(fifo, os.O_RDWR)` — `O_RDWR`
  specifically, so the writer never blocks and the node reads line by line.
- **Failure injection:** `kill -9` for a hard drop, `SIGSTOP`/`SIGCONT` to freeze a node
  that still holds its sockets but answers nothing.
- **Reading state without races:** open `.dirs` and any sqlite read-only
  (`file:{path}?mode=ro`); never copy or write inside a live node's directory.
- **Waiting on logs:** tail from the offset captured at node start, otherwise a restart
  makes `wait_log` match lines from the previous life of the node.

---

## 3. Building a stand from scratch

A stand is a harness process (any scripting language) that owns N node processes on one
host and observes their on-disk state. Nothing below depends on a particular repository
layout.

**Step 1 — addresses.** Give every node its own loopback address so the network layer
behaves as it would between real hosts. Ports alone are not enough: connection dedup,
discovery and peer sharing all key on the address, so many-nodes-one-address silently
disables exactly the code paths worth testing.

**Step 2 — core hooks.** The core needs three test-only entry points, each read from an
environment variable and each marked as stand-only in the source: bind the listener to a
given address; disable the outgoing-dial logic for the seed node; lift the per-sender
transaction rate limit so the stand can generate load. Keep them out of every commit.

**Step 3 — a console entry point.** A headless binary that can create or join a network,
accept commands on stdin, and log verbosely. Two commands are enough to drive everything:
add a file to storage, and emit a transaction. Prefer a transaction type that needs no
balance, otherwise the stand has to bootstrap funds before it can generate any load.

**Step 4 — process control.** Launch each node with its own working directory, its own
log file, and stdin wired to a named pipe opened read-write (so the harness never blocks
writing a command). Track the child PIDs; kill by process tree, not by harness name.

**Step 5 — topology.** Start the seed first and wait for it to listen. Start the rest with
a settings file pointing at a peer, then have every node dial every other node explicitly.
Do not rely on automatic peer discovery to build the mesh — it may be gated for loopback,
and a stand that half-connects produces meaningless results.

**Step 6 — load.** Two generators running together: a transaction stream at a chosen rate,
and file additions of mixed sizes (kilobytes to tens of megabytes) at random nodes. Record
the size and hash of every file before deleting the source, and remember every transaction
you asked for.

**Step 7 — observation.** Every couple of minutes, read the on-disk state of all nodes and
compare: chain heights, the transaction set of each section, the control hashes at the
sealing interval, and the size *and hash* of every replicated file. Scan node logs for the
failure markers the core emits. Write one line per audit to a progress log so a long run
can be read after the fact.

**Step 8 — chaos (separate mode).** Kill nodes with an uncatchable signal, freeze them with
stop/continue, restart them, and let the harness revive whatever died. Keep this switchable:
a clean run proves correctness, a chaos run proves recovery, and mixing them makes failures
hard to attribute.

**Step 9 — guards.** Cap total generated bytes and refuse to add files when free disk space
drops below a threshold. Seed all randomness explicitly and log the seed.

**Step 10 — a verdict.** The run must end with an unambiguous pass/fail line and the
numbers behind it. A run whose output has to be interpreted by hand will be interpreted
optimistically.

---

## 4. Stand shapes

Each shape isolates a different failure class. The combined shape is the one that gates
changes; the others are diagnostic.

| Shape | Topology | What it finds |
|-------|----------|---------------|
| Star | one hub, N leaves, plus a light client | file loss and stalls across restarts, light-client saturation |
| Chain | every node knows only its neighbour | multi-hop propagation, split and repair of a broken path |
| Dense mesh | every node connects to every other | connection density, connection churn, duplicate-dial handling |
| **Combined** | dense mesh + transaction stream + file traffic | **consensus divergence, control splits, integrity under load** |
| Targeted repro | 2–3 nodes, one deterministic scenario | a single class, kept as a regression test after the fix |

Load profiles for the combined shape, chosen by environment variable:

- *production-like*: a few transactions per second network-wide (matching what the real
  network sees per section) — the profile a release must be clean on;
- *for growth*: an order of magnitude above that — where interaction bugs surface first;
- *stress*: pushed until sections hold thousands of transactions — finds the ceiling, and
  is not representative of production.

Run the clean version first; add chaos only once the clean run is green, so a failure can
be attributed.

---

## 5. Traps — verify the harness before trusting a metric

1. **`range` and section file names are hex.** A `\d+` parser silently reads `4f` as `4`.
   A run reported PASS while 16 of 49 sections actually diverged.
2. **File size is not integrity.** Corrupted replicas had the *correct* size and zeroes
   inside. Without sha256 against the original this entire class is invisible.
3. **Files in flight are not corruption.** Audits need a `min_age` or an mtime filter,
   otherwise half the alarms are false.
4. **Zombie nodes.** Killing the harness by its own name kills the conductor, not the
   node processes it spawned; the next run then joins a stale network and produces
   nonsense. Kill the process tree — match on something unique to the node command line.
5. **A stuck file may be a false alarm** when `Added/Downloaded` in the log refers to a
   *different* file id of the same owner — always compare full ids, never prefixes.
6. **Disk budget is mandatory.** N copies of large files fill a disk within an hour; one
   early run took the host to zero free space (ENOSPC). Keep a `statvfs` guard and delete
   generated sources immediately after hashing them.

---

## 6. Working with core fixes

**Before the commit:** run the combined DAG + DFS stand on the build you are about to
commit — clean profile first, then chaos. A change that only ran against a DAG-only or
DFS-only stand has not been tested: the failures worth catching live in the interaction
between bulk transfer and consensus traffic.

Stand hacks live in the stand worktree and must never appear in a commit. The ritual for
every commit: strip the hacks, commit, restore the hacks, rebuild the console. To split a
mixed working tree into meaningful commits, temporarily revert the unrelated hunk, commit
the first change, restore the hunk, commit the second.

**A/B a suspected fix** by running the same seed before and after, and comparing the
audit metrics rather than impressions. Keep the progress log of the "before" run: without
it there is nothing to prove the fix against, and a run that merely looks better is easy
to talk yourself into.

---

## 7. Root-cause checklist for a stuck or divergent artifact

1. Is the node alive? (`ps`, and is the log still moving) — note that `pgrep` on a path
   containing brackets does not match; search by `--login` instead.
2. Is the artifact physically present and the right size *and* hash?
3. For a file: how many times was it queued, and when did the loader last touch it?
   Silence after a point means it left `active_downloads` (erased, or lost to a restart).
4. Compare the last event with the node's `===== START` markers — an item queued *before*
   a restart means the queue died with the process.
5. For a transaction: was it approved, rejected, or never seen? A `TooSectionDiff`
   rejection on a healthy node means it arrived late — look at what was occupying the
   socket at that moment.
6. For a control mismatch: are the transaction *sets* equal? If yes, the data is fine and
   the problem is in when/how the control was computed, not in replication.
