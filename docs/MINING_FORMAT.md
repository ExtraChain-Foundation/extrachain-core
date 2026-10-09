# Mining state compatibility

Mining uses native ExtraCoin. The total emission limit is 10,000 ExC.

## Epochs follow block time

Every batch carries `parent_time`, the time of its parent block; voters refuse a batch whose value
differs from the parent they know. A height's state is built before the height has a time of its own,
so mining transitions use the parent's.

Epoch `e` covers parent times `[e * epoch_ms, (e + 1) * epoch_ms)` of the emission policy (30 s by
default). On the first section of a height whose parent time enters a new period, that epoch starts:
its budget is reserved and the current registrations are frozen. Periods no height reached never
start, and their budget is never reserved. At most eight epochs are in flight; a later one is skipped
the same way.

An epoch records its own schedule as block time passes:

- `challenge_section` is the last section of the height that started the next epoch. Its header is the
  challenge.
- The window opens on the first section after two certified descendants of that height
  (`challenge_section + 41`) and stores `proof_closes_ms`, the opening parent time plus the policy's
  `proof_window_ms` (10 s by default).
- The height whose parent time reaches `proof_closes_ms` is the window's last one and sets
  `proof_last_section`. Settlement follows two heights later (`proof_last_section + 41`).

Epoch ids in the policy are these periods, so `first_epoch` is a point in time and emission no longer
depends on the block rate. A policy with one-millisecond periods starts an epoch at every height.

## Stable epoch encoding

`MiningEpochState` is a MessagePack array with exactly twelve fields, in this order:

1. `network`
2. `epoch`
3. `budget_units`
4. `datasets`
5. `challenge`
6. `proof_first_section`
7. `proof_last_section`
8. `settled`
9. `rewards`
10. `claimed`
11. `challenge_section`
12. `proof_closes_ms`

`claimed` is a set of actor identifiers retained for historical serialization. New
epochs leave it empty. There is no individual claim API. The reader preserves a
nonempty historical value, and the state and witness checks bound its size and actor
identifier lengths. Settlement rejects a record that already has rewards or claims.
A settled record cannot settle again.

Do not remove or reorder fields when removing an unused API. The serialized bytes
are part of `mining_epoch_root`, the epoch leaves in `mining_state_root`, and epoch
membership proofs. The hash domains remain `EXC_MINING_EPOCH_V1` and
`EXC_MINING_STATE_ENTRY_V2`. The state's parameter leaf also commits `next_epoch`.

The decoder rejects missing and extra epoch fields, including the earlier nine- and
ten-field formats. The standard MessagePack array adapter alone is insufficient:
it accepts missing or extra fields. Successful decoding does not prove that the
decoded object has the same signed bytes. Snapshot loading also requires exact
reserialization and a matching authenticated finality proof.

## Peer update

The handshake requires `shadow_consensus_v7` for current Shadow access. v6 builds count
mining epochs in heights and their batches have no parent time; intermediate builds
advertised `shadow_consensus_v5` with two different mining layouts. They must update
before participating. This capability revision does not change the existing consensus
document protocol version or rewrite signed historical documents.

Current nodes also advertise `shadow_consensus_v5` and `shadow_consensus_v6` so an old
node does not block its own outgoing file requests to them. These advertisements permit
the old data path; only v7 grants Shadow access at the current node. Do not use v5 or v6
for current peer selection or admission. Test both directions with an actual old executable.

Old peers retain the existing restricted data and update paths. They cannot submit
mining work, earn new rewards, or participate in Shadow. Their handshake capabilities
are cleared in restricted mode. A new authenticated session after update can regain
access. Capability checks do not replace proof and state validation.

## Data handling

Snapshots of earlier layouts are not converted. A nine- or ten-field snapshot must not
be repaired by adding fields: that changes the root while its certificate still commits
to the old root. Do not reset emission counters or remove a snapshot to bypass this error.

The inspected nine-field data on hpc1 belongs to disposable test networks. Preserve
those results as evidence and create a fresh stand directory and network for this
revision. Do not reuse their snapshots, signed Shadow history, or activation bundle.
A pre-Shadow funded DAG fixture can seed a fresh test network.

If another deployment has confirmed nine-field history, stop the upgrade for that
network and retain its executable, snapshot, certificates, and DAG. It needs an
explicit, authenticated format transition and historical verifier before upgrade.
This repair does not claim to migrate such a network. Never select a consensus
format from a peer's preference or accept whichever candidate root happens to match.

## Verification

`mining-format` uses fixed bytes of the twelve-field layout, checked against an
independent MessagePack encoding, and the BLAKE3 roots they produce. It checks empty and nonempty claims, snapshots,
membership proofs, changed claims, field-count errors, invalid claim types and
bounds, and settlement rejection without state changes.

`consensus-mining-runtime` checks durable restart and replay. It also injects an
eleven-field epoch into a snapshot and requires a load failure without replacing the
file. Restoring the original snapshot permits normal recovery. `peer-identity`
checks restricted signed v5 sessions and authenticated access after update.

`shadow_legacy_update.py` also downloads the verified payload to a second old client
through the old node. All seven current nodes are paused during that download, so a
direct connection to them cannot establish a false success. The test resumes them
on failure as well as success, then checks the same-directory update and full audits.

Run these checks first, then the combined DAG + DFS and legacy-update profiles via
[the stand controller](STAND.md). Full branch acceptance remains subject to
[TESTING.md](TESTING.md); a short profile does not complete six-hour acceptance.
