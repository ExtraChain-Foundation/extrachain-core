# Signed Receive Framing

`NetworkManager::message_received` expects a nonempty serialized body followed
by a 64-byte signature. Reject messages of 64 bytes or fewer before duplicate
cache processing or unsigned length subtraction. Otherwise a shorter frame
throws `std::out_of_range`; a signature-only frame reaches the parser without
a body and unnecessarily mutates the duplicate cache.

The transport decoder rejects decryption failure and empty output, but that
does not establish this minimum signed-frame length. Keep the check at the
dispatcher boundary as well. Valid binary payload/trailer bytes, duplicate
handling and the disabled-node early return remain unchanged.

This is framing validation, not signature verification, authenticated peer
identity or a change to trust/bootstrap policy. It is not evidence that this
exception caused the separately tracked historical node SIGSEGV.

## Regression

Run `python3 tests/network_receive_frame.py` with a C++20 compiler available.
In an MSVC developer command prompt, set `RACCOON_TEST_CXX=cl.exe` first.
`RACCOON_TEST_SOURCE_ROOT` optionally selects another ExtraCore source tree.

The test compiles the actual production method prefix up to deserialization,
with duplicate-cache and parser-boundary observers. Six scenarios check all
lengths 0 through 64, a one-byte body, embedded NUL/non-ASCII bytes, duplicates,
disabled-node behavior and valid input following rejection. No socket,
cryptographic verification or full-node runtime is simulated or certified by
this fixture; deployment verification is recorded separately in the tracker.
