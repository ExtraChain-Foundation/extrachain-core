#!/usr/bin/env bash
# Seven real ExtraChain processes with Shadow Finality, DAG synchronization,
# ExDFS transfer, signed V2 intents, and durable finality receipts.

set -u

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
BUILD_DIR="${EXTRACHAIN_TEST_BUILD:-$SCRIPT_DIR/build}"
SEED="${1:-/tmp/gen-shadow-seed}"
BASE_PORT="${2:-17840}"
INTENT_COUNT="${EXTRACHAIN_SHADOW_INTENTS:-64}"
RUN_SECONDS="${EXTRACHAIN_SHADOW_RUN_SECONDS:-300}"
WORK="$(mktemp -d /tmp/exc-shadow-seven-XXXXXX)"
SYNC_WORK="$WORK/bootstrap"
NODE_RUN="$BUILD_DIR/extrachain-node-run"
BUNDLE="$BUILD_DIR/extrachain-shadow-bundle"
DAG_AUDIT="$BUILD_DIR/extrachain-dag-audit"
BARRIER="$WORK/barrier"
PIDS=()

cleanup() {
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    sleep 2
    for pid in "${PIDS[@]}"; do
        kill -9 "$pid" 2>/dev/null || true
    done
    for pid in "${PIDS[@]}"; do
        wait "$pid" 2>/dev/null || true
    done
}
trap cleanup EXIT

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    printf 'stand data: %s\n' "$WORK" >&2
    exit 1
}

[ -x "$NODE_RUN" ] || fail "node runner is absent: $NODE_RUN"
[ -x "$BUNDLE" ] || fail "Shadow bundle tool is absent: $BUNDLE"
[ -x "$DAG_AUDIT" ] || fail "DAG audit tool is absent: $DAG_AUDIT"
[ -d "$SEED/dag" ] || fail "seed DAG is absent: $SEED"

printf '=== bootstrap seven independent nodes ===\n'
EXTRACHAIN_TEST_BUILD="$BUILD_DIR" \
EXTRACHAIN_TEST_WORK="$SYNC_WORK" \
EXTRACHAIN_TEST_DFS_BYTES=1048576 \
    "$SCRIPT_DIR/multi_console_sync.sh" "$SEED" 6 "$BASE_PORT" \
    || fail "DAG and ExDFS bootstrap failed"

NODE_HOMES=("$SYNC_WORK/server/data")
for index in $(seq 1 6); do
    NODE_HOMES+=("$SYNC_WORK/client$index/data")
done

printf '=== create and verify local Shadow ceremony ===\n'
PREPARED=""
for index in $(seq 0 6); do
    role="joiner"
    [ "$index" -eq 0 ] && role="seed"
    prepared="$($BUNDLE --prepare "${NODE_HOMES[$index]}" "$role" 2>&1)" \
        || {
            printf '%s\n' "$prepared" >&2
            fail "Shadow transition preparation failed for node $index"
        }
    marker="$(printf '%s\n' "$prepared" | sed -n 's/.*prepared network=\([^ ]*\) boundary=\([^ ]*\).*/\1:\2/p')"
    [ -n "$marker" ] || fail "node $index did not report its transition boundary"
    if [ -z "$PREPARED" ]; then
        PREPARED="$marker"
    elif [ "$PREPARED" != "$marker" ]; then
        fail "node $index has a different transition boundary"
    fi
done
"$BUNDLE" "${NODE_HOMES[0]}" "${NODE_HOMES[@]}" \
    >"$WORK/bundle.log" 2>&1 || {
        tail -40 "$WORK/bundle.log" >&2
        fail "Shadow bundle creation failed"
    }
LEADER_INDEX="$(sed -n 's/.*leader_index=\([0-6]\).*/\1/p' "$WORK/bundle.log")"
[ -n "$LEADER_INDEX" ] || fail "the first Shadow leader was not reported"

printf '=== start full-mesh Finality committee ===\n'
mkdir -p "$BARRIER"
for index in $(seq 0 6); do
    if [ "$index" -eq 0 ]; then
        parent="$SYNC_WORK/server"
        role="seed"
    else
        parent="$SYNC_WORK/client$index"
        role="joiner"
    fi
    port=$((BASE_PORT + 20 + index))
    (
        cd "$parent" || exit 73
        EXC_BIND_IP="127.0.0.$((index + 1))" exec "$NODE_RUN" committee data "$role" "$index" "$port" "$((BASE_PORT + 20))" 7 \
            "$([ "$index" -eq 0 ] && printf '%s' "$INTENT_COUNT" || printf '0')" "$RUN_SECONDS" "$BARRIER"
    ) >"$WORK/node-$index.log" 2>&1 &
    PIDS+=("$!")
    # Each node connects only to lower indexes. Give the new listener time to
    # enter its accept loop before the next node starts its deterministic dials.
    sleep 1
done

barrier_deadline=$(( $(date +%s) + 75 ))
while [ "$(find "$BARRIER" -maxdepth 1 -type f -name 'node-*' | wc -l)" -ne 7 ]; do
    [ "$(date +%s)" -ge "$barrier_deadline" ] && fail "the committee did not reach the start barrier"
    sleep 1
done
touch "$BARRIER/go"

result=0
for index in $(seq 0 6); do
    if ! wait "${PIDS[$index]}"; then
        result=1
        printf '%s\n' "--- node $index ---" >&2
        tail -60 "$WORK/node-$index.log" >&2
    fi
done
PIDS=()
[ "$result" -eq 0 ] || fail "one or more Finality nodes failed"
grep -q "finalized intents=$INTENT_COUNT" "$WORK/node-0.log" \
    || fail "the submitted intents did not reach durable finality"
for index in $(seq 0 6); do
    grep -Eq "finalized=[1-9][0-9]*" "$WORK/node-$index.log" \
        || fail "node $index did not observe a finalized checkpoint"
done

printf '=== audit DAG and balance snapshots ===\n'
CACHE_SNAPSHOT=""
for index in $(seq 0 6); do
    role="joiner"
    [ "$index" -eq 0 ] && role="seed"
    "$DAG_AUDIT" "${NODE_HOMES[$index]}" "$role" >"$WORK/audit-$index.log" 2>&1 \
        || {
            tail -60 "$WORK/audit-$index.log" >&2
            fail "DAG or balance audit failed for node $index"
        }
    snapshot="$(sed -n 's/.*balance cache: section=\([^ ]*\).*hash=\([^ ]*\).*/\1:\2/p' "$WORK/audit-$index.log")"
    [ -n "$snapshot" ] || fail "node $index did not report a balance snapshot"
    if [ -z "$CACHE_SNAPSHOT" ]; then
        CACHE_SNAPSHOT="$snapshot"
    elif [ "$CACHE_SNAPSHOT" != "$snapshot" ]; then
        fail "node $index has a different logical balance snapshot"
    fi
done

printf '=== protocol failure, epoch, recovery, and light-client checks ===\n'
"$BUILD_DIR/extrachain-consensus-network-tests" >"$WORK/network.log" 2>&1 \
    || fail "committee protocol checks failed"
"$BUILD_DIR/extrachain-shadow-recovery-tests" >"$WORK/recovery.log" 2>&1 \
    || fail "recovery checks failed"

printf 'PASS: seven processes finalized %s intents with one audited balance snapshot\n' "$INTENT_COUNT"
printf 'stand data: %s\n' "$WORK"
