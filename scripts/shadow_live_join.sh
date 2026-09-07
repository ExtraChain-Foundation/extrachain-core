#!/usr/bin/env bash
# Live-join while the chain is growing: run the shadow soak (committee finalizes
# intent batches, the DAG tip moves every interval) and, once growth is under way,
# attach a fresh 8th node that must catch a MOVING target through plain sync.
#
# usage: shadow_live_join.sh [seed] [base-port]
set -u
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
SEED="${1:-$SCRIPT_DIR/../../seeds/gen-25k}"
BASE_PORT="${2:-17840}"
BUILD_DIR="${EXTRACHAIN_TEST_BUILD:-$SCRIPT_DIR/../tests/build}"
WORK="$(mktemp -d /tmp/exc-live-join-XXXXXX)"
GROW_AHEAD="${EXC_LIVE_JOIN_AHEAD:-120}"   # how far past the observed tip the joiner must reach

log() { printf '%s %s\n' "$(date +%H:%M:%S)" "$*"; }

# A long soak supplies the moving tip: many intents, big window, no early exit
EXC_SHADOW_WORK="$WORK/soak" EXC_SHADOW_SENDERS="${EXC_SHADOW_SENDERS:-6}" \
EXC_SHADOW_PER_SENDER="${EXC_SHADOW_PER_SENDER:-48}" \
EXC_SHADOW_RUN_SECONDS="${EXC_SHADOW_RUN_SECONDS:-420}" \
EXC_SHADOW_DEADLINE_S="${EXC_SHADOW_DEADLINE_S:-720}" EXC_DEBUG_LOG=1 \
    bash "$SCRIPT_DIR/shadow_soak.sh" "$SEED" "$BASE_PORT" > "$WORK/soak.log" 2>&1 &
SOAK_PID=$!
JOIN_PID=""
trap 'kill "$SOAK_PID" 2>/dev/null; [ -n "$JOIN_PID" ] && kill "$JOIN_PID" 2>/dev/null' EXIT

server_range() {
    sed -n 's/.*"last":"\([0-9]*\)".*/\1/p' "$WORK/soak/bootstrap/server/data/dag/range" 2>/dev/null
}

log "waiting for the committee to start growing the chain"
deadline=$(( $(date +%s) + 420 ))
tip=""
while :; do
    tip="$(server_range)"
    [ -n "$tip" ] && [ "$tip" -gt 25000 ] && break
    kill -0 "$SOAK_PID" 2>/dev/null || { tail -20 "$WORK/soak.log"; echo "FAIL: soak died before growth"; exit 1; }
    [ "$(date +%s)" -ge "$deadline" ] && { tail -20 "$WORK/soak.log"; echo "FAIL: no growth within 420s"; exit 1; }
    sleep 2
done
TARGET=$((tip + GROW_AHEAD))
log "tip is at $tip and moving; joiner must reach $TARGET"

DFS_LINE="$(grep "DFS payload owner=" "$WORK/soak/bootstrap/server/console.log" | tail -1)"
DFS_OWNER="$(printf '%s\n' "$DFS_LINE" | sed -n 's/.*owner=\([^ ]*\).*/\1/p')"
DFS_SIZE="$(printf '%s\n' "$DFS_LINE" | sed -n 's/.*size=\([0-9]*\).*/\1/p')"
[ -n "$DFS_OWNER" ] && [ -n "$DFS_SIZE" ] || { echo "FAIL: no DFS payload metadata in bootstrap log"; exit 1; }
log "joiner must also fetch ExDFS payload owner=$DFS_OWNER size=$DFS_SIZE"

mkdir -p "$WORK/joiner"
( cd "$WORK/joiner" && exec "$BUILD_DIR/extrachain-node-run" join data 127.0.0.1 "$TARGET" \
      $((BASE_PORT + 60)) $((BASE_PORT + 20)) "$DFS_OWNER" "combined-network.bin" "$DFS_SIZE" ) \
      > "$WORK/joiner.log" 2>&1 &
JOIN_PID=$!

join_deadline=$(( $(date +%s) + 420 ))
verdict=""
while :; do
    if ! kill -0 "$JOIN_PID" 2>/dev/null; then
        wait "$JOIN_PID"; rc=$?
        [ "$rc" -eq 0 ] && verdict="joined" || verdict="join-exit-$rc"
        break
    fi
    [ "$(date +%s)" -ge "$join_deadline" ] && { verdict="join-timeout"; kill "$JOIN_PID" 2>/dev/null; break; }
    sleep 3
done

joiner_last="$(sed -n 's/.*"last":"\([0-9]*\)".*/\1/p' "$WORK/joiner/data/dag/range" 2>/dev/null)"
final_tip="$(server_range)"
log "joiner verdict=$verdict joiner_last=${joiner_last:-none} committee tip=$final_tip"

wait "$SOAK_PID"; soak_rc=$?
grep -E "PASS|FAIL" "$WORK/soak.log" | tail -2

if [ "$verdict" = "joined" ] && [ "$soak_rc" -eq 0 ]; then
    log "auditing the joined node (full replay + balance snapshot)"
    if ! "$BUILD_DIR/extrachain-dag-audit" "$WORK/joiner/data" joiner > "$WORK/joiner-audit.log" 2>&1; then
        tail -20 "$WORK/joiner-audit.log"
        echo "LIVE-JOIN FAIL: joined node did not pass the DAG/balance audit"
        echo "stand data: $WORK"
        exit 1
    fi
    grep -m1 "AUDIT PASS" "$WORK/joiner-audit.log"
    echo "LIVE-JOIN PASS: fresh node caught the moving tip at $TARGET, fetched the ExDFS payload and passed the audit"
    echo "stand data: $WORK"
    exit 0
fi
echo "LIVE-JOIN FAIL: verdict=$verdict soak_rc=$soak_rc"
echo "stand data: $WORK"
exit 1
