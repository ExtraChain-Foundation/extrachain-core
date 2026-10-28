# Activate Shadow on an existing test network

This procedure moves a running legacy network to Shadow finality on the same data
directories. It uses `extrachain-shadow-bundle`, which generates every committee and
governance key in one process. That is acceptable for a test network. A production
network needs independent offline key custody and `extrachain-shadowctl` authorization
shares instead.

## Plan the committee

- A committee has 4 to 64 validators. It tolerates `(n - 1) / 3` silent or faulty
  validators and needs `2n / 3 + 1` votes per height. 4 tolerates one; 7 and 8
  tolerate two.
- Each validator is a separate node process with its own data directory, port, and
  node identifier. Two validators can share a server; that server then counts as two
  faults when it stops. With 8 validators on 4 servers, one server can stop.
- Validators run the DAG in full mode (`--dag-mode full` in the console client).
- Every other node of the network becomes an observer. Older binaries keep restricted
  data access and do not take part in Shadow.

## 1. Reach a section boundary on the running network

Shadow starts at a DAG section that is a multiple of 20. A legacy transaction is
stamped with the sender's current section plus one, so sections advance only as
transactions are accepted. Do this while the network still runs its current binary:
stop writes from users, then submit one transaction at a time, waiting for each to be
accepted, until every node reports the same current section and that section is a
multiple of 20. Stop every node.

Builds before the short-tail sync correction can stall in legacy mode after bootstrap.
They request matching closed controls instead of the missing next section and then
refuse further transfers. Use the previous binary for those builds. With the correction,
a two-node REST check accepts 42 transfers across two control boundaries and three
more after restart. All 45 transactions are present and equal on both stopped nodes;
DAG, control, and balance audits pass. Check the build revision before using that path.

## 2. Install the new binary and seal the legacy DAG

Replace the binary on every node. Do not start the nodes yet.

On every validator, with the node stopped:

```sh
printf '%s\n%s\n' "$LOGIN" "$PASSWORD" | extrachain-shadow-bundle --login-hash
extrachain-shadow-bundle --prepare /path/to/data <login-hash>
```

`--prepare` seals the legacy DAG at the boundary and prints
`prepared network=<id> boundary=<section>`. Every node must print the same pair. If
the current section is not a boundary, it fails and prints how many more transactions
reach the next one; start the network on the previous binary, submit them, and repeat
steps 1 and 2. The transition seal and the corrected legacy transfer path have separate
checks; a transfer check does not replace the seal checks below.

## 3. Generate the documents

Run the ceremony on one machine. It needs:

- a copy of the stopped data directory of the node whose profile created the network:
  that profile holds the network actor, which signs the validator set;
- one directory per validator that contains only that validator's `.settings` file.

```sh
export EXC_SHADOW_SEED_LOGIN_HASH=<login hash of the network creator profile>
export EXC_SHADOW_KEYS_DIR=/secure/new-directory
extrachain-shadow-bundle /path/to/seed-copy homes/v0 homes/v1 ... homes/v7
```

The tool writes `consensus/` into each validator directory and keeps every generated
private key in `EXC_SHADOW_KEYS_DIR` with owner-only permissions:

| File | Use |
| --- | --- |
| `governance-<i>.msgpack` | Five keys, three sign a committee change (`shadowctl sign`). |
| `recovery-<i>.msgpack` | Five keys, four sign a recovery. |
| `validator-<i>-actor.msgpack`, `validator-<i>-consensus.msgpack` | Validator account and vote key. |
| `ceremony.json` | Network, activation section, and the validator list. |

Store the keys directory offline. Losing it means the committee can never change.

## 4. Distribute and start

Copy `homes/v<i>/consensus/` to `consensus/` inside validator `i`'s data directory.
Each observer receives the same documents except `identity.msgpack`. Check each copy:

```sh
extrachain-shadowctl verify-config /path/to/data/consensus <network-id>
```

Start every validator, then the observers. Each logs
`Shadow certification enabled for epoch 1 with <n> validators` and then
`Finalized height <h> at Dag section <s>` while the committee has a quorum. An idle
committee only logs timeout certificates; heights advance when requests arrive.

## After activation

- A regular transfer sent through `ExtraChainNode::send_transaction` becomes a signed
  intent with the next account nonce and a 10,000-height window. Its hash is logged as
  `Transfer ... submitted as intent <hash>`; the committee writes the final transaction,
  which has a different hash. Other legacy transaction types are refused with
  `IntentRequired`.
- Console clients that include the Crow REST API must build with `CROW_USE_BOOST`.
  Core headers include Boost.Asio, and standalone Asio conflicts with it.
