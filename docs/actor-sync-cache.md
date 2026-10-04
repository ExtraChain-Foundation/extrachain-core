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

Difference generation sorts temporary pointers to the bucket-owned IDs, then
copies each selected ID exactly once into the returned owning vector. Sorting
ActorId values themselves invoked validating assignments repeatedly on the
owner thread. No references escape the call, and the resulting snapshot moves
into the actor-response worker without another vector copy.

Build `extrachain-actor-filter-tests` with `EXTRACHAIN_BUILD_DB_TESTS=ON` and run
CTest `extrachain-actor-filter-cache`. It compares hashes and response ordering
with the previous algorithm for duplicates, additions, replacement, malformed
lengths and a large unchanged index. These tests do not establish full-network
load capacity or certify the receive queue's overload policy.
The source-header work-counter fixture also checks 100 full differences of a
35000-actor index: one copy per returned ID, no moves/assignments of ActorId
values, no rescanning of unchanged hash inputs. Native tests verify returned
values survive replacement and destruction of the synchronizer.
