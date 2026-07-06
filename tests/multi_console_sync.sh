#!/usr/bin/env bash
# Live multi-console synchronization test on the local machine.
#
# Brings up one server console seeded with a generated chain and N fresh client
# consoles that join it over WebSocket (127.0.0.1) and sync the whole chain.
# Verifies every client ends up with the server's range, pack count and a
# bytewise check, then tears everything down.
#
#   tests/multi_console_sync.sh [SEED_DIR] [N_CLIENTS] [PORT]
#
# SEED_DIR defaults to /tmp/gen-25k (produced by extrachain-gen-sections).
#
# Each console gets its OWN home dir as launch cwd (the .extrachain-console.lock
# single-instance lock lives in the launch cwd), with its data under home/data
# (--current-dir is a relative subdir of the launch cwd).

set -u

NODE_RUN="/Users/draqonic/ExC/extrachain-core/tests/build/extrachain-node-run"
SYNC_CHECK="/Users/draqonic/ExC/extrachain-core/tests/build/extrachain-sync-check"
SEED="${1:-/tmp/gen-25k}"
N="${2:-3}"
PORT="${3:-17593}"
LOGIN="gen-login"
PASSWORD="gen-password"

WORK="/tmp/exc-multi-sync"
PIDS=()

cleanup() {
    echo "--- cleanup ---"
    for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done
    sleep 1
    for p in "${PIDS[@]}"; do kill -9 "$p" 2>/dev/null; done
}
trap cleanup EXIT

range_last() { [ -f "$1/dag/range" ] && sed -n 's/.*"last":"\([0-9]*\)".*/\1/p' "$1/dag/range"; }
count_packs() { ls "$1/dag/packs" 2>/dev/null | grep -c '\.pack$'; }

[ -x "$NODE_RUN" ] || { echo "FAIL: node-run binary not found at $NODE_RUN"; exit 1; }
[ -d "$SEED/dag" ] || { echo "FAIL: seed dir $SEED has no dag/"; exit 1; }

SERVER_LAST="$(range_last "$SEED")"
SERVER_PACKS="$(count_packs "$SEED")"
echo "=== seed: range.last=$SERVER_LAST packs=$SERVER_PACKS ==="

rm -rf "$WORK"; mkdir -p "$WORK"

# --- server: own home, full copy of the seed as its data ----------------------
mkdir -p "$WORK/server"
cp -R "$SEED" "$WORK/server/data"
echo "=== starting server (port $PORT) ==="
( cd "$WORK/server" && exec "$NODE_RUN" serve data ) >"$WORK/server/console.log" 2>&1 &
PIDS+=($!)

for i in $(seq 1 90); do
    lsof -iTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1 && break
    sleep 1
done
if ! lsof -iTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
    echo "FAIL: server not listening on $PORT after 90s"; tail -8 "$WORK/server/console.log"; exit 2
fi
echo "server is listening on $PORT"

# --- clients: empty home; node-run creates its own distinct profile and joins ---
for c in $(seq 1 "$N"); do
    HOME_C="$WORK/client$c"; DATA_C="$HOME_C/data"
    mkdir -p "$DATA_C"
    echo "=== starting client$c (join) ==="
    ( cd "$HOME_C" && exec "$NODE_RUN" join data 127.0.0.1 "$SERVER_LAST" ) \
        >"$HOME_C/console.log" 2>&1 &
    PIDS+=($!)
    sleep 1
done

# --- poll clients until they reach the server's range (or time out) -----------
echo "=== waiting for sync (target range.last=$SERVER_LAST) ==="
DEADLINE=$(( $(date +%s) + 300 ))
while :; do
    all=1; line=""
    for c in $(seq 1 "$N"); do
        last="$(range_last "$WORK/client$c/data")"; last="${last:-0}"
        packs="$(count_packs "$WORK/client$c/data")"
        line="$line  c$c=$last/${packs}p"
        [ "$last" = "$SERVER_LAST" ] || all=0
    done
    echo "progress:$line"
    [ "$all" = "1" ] && { echo "=== all clients reached range.last=$SERVER_LAST ==="; break; }
    [ "$(date +%s)" -ge "$DEADLINE" ] && { echo "=== TIMEOUT ==="; break; }
    sleep 5
done

# --- verify ------------------------------------------------------------------
echo "=== verification ==="
rc=0
for c in $(seq 1 "$N"); do
    D="$WORK/client$c/data"
    last="$(range_last "$D")"; last="${last:-0}"
    packs="$(count_packs "$D")"
    hot="$(ls "$D/dag/hot" 2>/dev/null | wc -l | tr -d ' ')"
    if [ "$last" = "$SERVER_LAST" ]; then status="OK"; else status="FAIL(range $last != $SERVER_LAST)"; rc=1; fi
    echo "client$c: range.last=$last packs=$packs hot=$hot -> $status"
done

if [ -x "$SYNC_CHECK" ] && [ "$(count_packs "$WORK/client1/data")" -gt 0 ]; then
    echo "=== bytewise check: client1 packs vs seed ==="
    "$SYNC_CHECK" "$WORK/client1/data" 2>/dev/null | sed 's/^/  /'
fi

echo "=== result: $([ $rc -eq 0 ] && echo PASS || echo FAIL) ==="
exit $rc
