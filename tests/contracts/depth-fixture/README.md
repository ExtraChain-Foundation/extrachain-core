# Nested depth replay fixture

This contract stores invocation depth plus one. The `walk` method calls each contract
in the supplied path. The C++ test compares normal execution with restoration from
DFS checkpoints and signed DAG records after it removes the local head cache.

Build the fixture from this directory with the same Rust toolchain as the SDK:

```sh
cargo build --locked --offline --release
cp target/wasm32v1-none/release/extrachain_depth_fixture.wasm ../depth-fixture.wasm
```

Keep the source, lock file and WASM fixture together. Run `contract-depth-replay`
after a fixture change. The test needs the Core library and local DFS storage.
