# ExtraChain AssemblyScript contracts

This directory contains an independent AssemblyScript contract SDK. It does not use the Rust SDK or the Rust build chain.

The fixed entry point implements ExtraChain contract ABI 3. User code stays in `assembly/contract.ts`. Generated component selection stays in `assembly/generated.ts`. The build entry point stays in `assembly/index.ts`.

The production toolchain package contains a pinned Node.js 24 LTS runtime, AssemblyScript 0.28.20, this SDK, the component library, the catalog, and the templates. A client installs that package from the trusted ExtraChain DFS address. A contract build does not run `npm install` and does not read global developer tools.

DFS write effects do not write arbitrary data. A client first uploads an immutable file. The contract then binds a logical key to its verified file ID and BLAKE3 content hash. A tombstone removes the logical binding. It does not edit or erase an immutable DFS object.
