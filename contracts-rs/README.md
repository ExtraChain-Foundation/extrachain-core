# ExtraChain Rust Contract SDK

Use Rust `1.97.1` or later. The workspace builds for `wasm32v1-none`. Contract code does not use
the Rust standard library.

Build all standard modules from this directory:

```sh
cargo build --release --workspace --target wasm32v1-none
```

Release modules are in `target/wasm32v1-none/release/`. The release profile uses size
optimization, link-time optimization, one code generation unit, stripped symbols, and aborting
panics.

## Contract API

Define a state struct and derive `ContractState`. The state version and field order form the
stored state format. Mark the owner field with `#[owner]` when the contract has owner-only
methods.

Use `#[contract]` on the implementation. Exported methods use these attributes:

- `#[init]` initializes empty state.
- `#[call]` changes state.
- `#[query]` reads state.
- `#[owner_only]` checks the direct caller against the `#[owner]` field.
- `#[authorize_upgrade]` approves a new module.
- `#[migrate]` changes state for a new module version.

The macros decode a MessagePack argument array, decode and encode versioned state, create the
runtime context, collect events and effects, and generate the fixed ExtraChain entry point. User
code contains business rules only.

```rust
#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};
use extrachain_contract_sdk::{Context, ContractResult, ContractState, contract};

#[derive(Default, ContractState)]
#[state(version = 1)]
struct Counter {
    #[owner]
    owner: String,
    value: u64,
}

#[contract]
impl Counter {
    #[init]
    fn init(&mut self, ctx: &Context<'_>) -> ContractResult<()> {
        self.owner = ctx.caller().to_string();
        Ok(())
    }

    #[call]
    #[owner_only]
    fn add(&mut self, value: u64) -> ContractResult<u64> {
        self.value = self.value.checked_add(value).ok_or("Counter overflow")?;
        Ok(self.value)
    }

    #[query]
    fn get(&self) -> ContractResult<u64> {
        Ok(self.value)
    }
}
```

Use `ActorId`, `NonZeroAmount`, `BoundedString`, `StateMap`, and `StateSet` for checked inputs and
bounded, deterministic state. Signed integers, optional values, vectors, tuples, and derived value
structs have typed codecs. Floating-point values do not implement `ContractValue` and cannot
appear in an exported method or stored state.

Contract code cannot use files, sockets, clocks, random data, threads, or operating system calls.
Use `Context` to emit events, declare a typed child call, declare fungible or NFT changes, bind
verified DFS data, or read verified DAG and DFS proofs. Use the component library for ownership,
roles, pause, replay protection, ledgers, escrow, multisig, DAG checks, and DFS binding state.

ExtraChain stores one current local state and periodic approved checkpoints in ExDFS. The DAG
stores ordered calls and the previous and result state hashes. Keep all operations deterministic.
Check integer operations for overflow. An invalid operation must return an error without a state
change.

The `fungible-token` and `non-fungible-token` packages are the standard token contracts. The
`message-claim` package is a neutral example that stores a message, issues a transferable unique
token, and returns and removes the message when its owner redeems the token.
