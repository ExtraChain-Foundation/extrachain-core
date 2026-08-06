# ExtraChain Rust Contract SDK

Use Rust `1.97.1` or later. The workspace builds for `wasm32v1-none` and does not use the Rust
standard library.

Build all contract modules from this directory:

```sh
cargo build --release --workspace
```

The release modules are in `target/wasm32v1-none/release/`. A contract implements the `Contract`
trait and uses `export_contract!` to export the ExtraChain ABI.

Contract code cannot use files, sockets, clocks, random data, threads, or operating system calls.
Persistent state is the binary state value in each request and response. ExtraChain stores approved
state revisions in ExDFS.

Use the SDK `Encoder` and `Decoder` for MessagePack arguments and results. Keep all operations
deterministic. Check all integer operations for overflow. Return a failure response without a state
change when an operation is not valid.

The `fungible-token` package is the standard token contract. The `message-claim` package is a
neutral example that stores a message, issues a transferable unique token, and returns and removes
the message when the current owner redeems the token.
