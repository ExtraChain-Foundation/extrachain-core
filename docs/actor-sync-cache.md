# Actor Synchronization Hash Cache

ActorSynchronizer stores unique ActorIds in their existing 256 FNV buckets.
Only a newly inserted actor invalidates its bucket hash; repeated requests reuse
hashes for unchanged buckets. Replacement resets all buckets and hashes. Copies
own independent actor sets and cache state. Like the original synchronizer,
access is restricted to its owner thread, not concurrent readers/writers.

The protocol is unchanged: identical FNV parameters, lexicographically sorted
unique actor strings, concatenation, empty-bucket hash and native-endian 2048-byte
serialization. Responses retain globally sorted ActorId order. Existing short
and overlong request interpretation is preserved; protocol validation and
endianness migration are outside this optimization.

Build `extrachain-actor-filter-tests` with `EXTRACHAIN_BUILD_DB_TESTS=ON` and run
CTest `extrachain-actor-filter-cache`. It compares hashes and response ordering
with the previous algorithm for duplicates, additions, replacement, malformed
lengths and a large unchanged index. These tests do not establish full-network
load capacity or certify the receive queue's overload policy.
