#!/usr/bin/env bash
# Our diagnostic variant of tests/shadow_multi_process.sh.
#
# Differences from the upstream scenario, each one earned by a failed run:
#   * intents are spread over SENDERS nodes instead of all coming from node 0,
#     so a single sender never exceeds maximum_nonce_gap / maximum_sender_intents (64);
#   * the run ends as soon as every node reports its intents finalized, instead of
#     always burning the full window — with a generous deadline for Debug builds;
#   * InvalidRoot, lost quorum and stalled voting are failures in their own right,
#     reported with the evidence rather than hidden behind "one or more nodes failed";
#   * every run keeps its work directory and prints a per-node summary table.
#
# usage: shadow_soak.sh [seed] [base-port]
# env:   EXC_SHADOW_SENDERS      how many nodes submit intents      (default 4)
#        EXC_SHADOW_PER_SENDER   intents per submitting node        (default 32)
#        EXC_SHADOW_RUN_SECONDS  per-node run window                (default 240)
#        EXC_SHADOW_DEADLINE_S   harness deadline for the committee (default 300)
#        EXC_SHADOW_HOLD_S       keep a passed committee alive this long (default 0)
#        EXC_SHADOW_ALLOWED_DEAD this many committee nodes may die (chaos kills) and the
#                                run still passes on the survivors (default 0)
#        EXC_SHADOW_DFS_BYTES    every node also publishes an ExDFS file of this size;
#                                the run passes only if it reaches every node (default 0)

set -u

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
REPO_DIR="$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)"
CORE_TESTS="${EXC_CORE_TESTS:-$REPO_DIR/tests}"
BUILD_DIR="${EXTRACHAIN_TEST_BUILD:-$CORE_TESTS/build}"
SEED="${1:-/tmp/gen-shadow-seed}"
BASE_PORT="${2:-17840}"
SENDERS="${EXC_SHADOW_SENDERS:-4}"
PER_SENDER="${EXC_SHADOW_PER_SENDER:-32}"
RUN_SECONDS="${EXC_SHADOW_RUN_SECONDS:-240}"
# The harness must outlive the nodes' own window, otherwise their scheduled exit
# races our deadline and a normal end-of-run looks like a crash.
DEADLINE_S="${EXC_SHADOW_DEADLINE_S:-$((RUN_SECONDS + 120))}"
NODE_COUNT=7
DFS_BYTES="${EXC_SHADOW_DFS_BYTES:-0}"
export EXC_DFS_BYTES="$DFS_BYTES"
ALLOWED_DEAD="${EXC_SHADOW_ALLOWED_DEAD:-0}"
# Nodes found dead when the watch loop ends (chaos kills); set once, before cleanup.
DEAD_NODES=""
is_dead() { case " $DEAD_NODES " in *" $1 "*) return 0 ;; esac; return 1; }

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum < "$1" | cut -d' ' -f1
    else shasum -a 256 < "$1" | cut -d' ' -f1; fi
}

# One line per published file: "<publisher index> <owner> <file id> <size>".
dfs_published() {
    for index in $(seq 0 $((NODE_COUNT - 1))); do
        # A file whose publisher died may never have reached anyone: not required.
        is_dead "$index" && continue
        sed -n "s/^\[node-run\] DFS stored owner=\([0-9a-f]*\) file_id=\([0-9a-f]*\) size=\([0-9]*\).*/$index \1 \2 \3/p" \
            "$WORK/node-$index.log" 2>/dev/null
    done
}

# ExDFS replication audit: every file a committee node published has to sit on
# every node byte for byte. With report=1 prints one line per node; returns 0
# only when the mesh is complete. A corrupt copy counts separately from a
# missing one — they point at different code.
dfs_audit() {
    local report="$1" complete=1 published total
    published="$(dfs_published)"
    total="$(printf '%s\n' "$published" | grep -c .)"
    if [ "$total" -eq 0 ]; then
        [ "$DFS_BYTES" -eq 0 ] && return 0
        [ "$report" = 1 ] && echo "dfs: no node published a file"
        return 1
    fi
    for index in $(seq 0 $((NODE_COUNT - 1))); do
        if is_dead "$index"; then
            [ "$report" = 1 ] && printf 'dfs: node %s died during the run; not audited\n' "$index"
            continue
        fi
        local have=0 corrupt=0 publisher owner file_id size src dst
        while read -r publisher owner file_id size; do
            [ -n "$file_id" ] || continue
            src="${NODE_HOMES[$publisher]}/dfs/$owner/$file_id"
            dst="${NODE_HOMES[$index]}/dfs/$owner/$file_id"
            [ -f "$dst" ] || continue
            if [ "$(wc -c < "$dst" | tr -d ' ')" = "$size" ] && [ "$(sha256_of "$dst")" = "$(sha256_of "$src")" ]; then
                have=$((have + 1))
            else
                corrupt=$((corrupt + 1))
            fi
        done <<<"$published"
        [ "$have" -eq "$total" ] || complete=0
        if [ "$report" = 1 ]; then
            printf 'dfs: node %s has %s/%s files' "$index" "$have" "$total"
            [ "$corrupt" -gt 0 ] && printf ' (%s corrupt)' "$corrupt"
            printf '\n'
        fi
    done
    [ "$complete" -eq 1 ]
}

WORK="${EXC_SHADOW_WORK:-$(mktemp -d /tmp/exc-shadow-soak-XXXXXX)}"
mkdir -p "$WORK"
SYNC_WORK="$WORK/bootstrap"
NODE_RUN="$BUILD_DIR/extrachain-node-run"
BUNDLE="$BUILD_DIR/extrachain-shadow-bundle"
DAG_AUDIT="$BUILD_DIR/extrachain-dag-audit"
SHADOW_VERIFY="$SCRIPT_DIR/shadow_verify.py"
BARRIER="$WORK/barrier"
PIDS=()

cleanup() {
    [ "${#PIDS[@]}" -eq 0 ] && return 0
    for pid in "${PIDS[@]}"; do kill "$pid" 2>/dev/null || true; done
    local deadline=$(( $(date +%s) + 15 ))
    local alive=1
    while [ "$alive" -eq 1 ] && [ "$(date +%s)" -lt "$deadline" ]; do
        alive=0
        for pid in "${PIDS[@]}"; do
            kill -0 "$pid" 2>/dev/null && alive=1
        done
        [ "$alive" -eq 1 ] && sleep 1
    done
    for pid in "${PIDS[@]}"; do
        kill -0 "$pid" 2>/dev/null && kill -9 "$pid" 2>/dev/null || true
    done
    for pid in "${PIDS[@]}"; do wait "$pid" 2>/dev/null || true; done
    PIDS=()
}
trap cleanup EXIT

log()  { printf '%s %s\n' "$(date +%H:%M:%S)" "$*"; }
fail() {
    printf 'FAIL: %s\n' "$1" >&2
    summary >&2
    printf 'stand data: %s\n' "$WORK" >&2
    exit 1
}

# How far a node actually got. The periodic counter sample stops the moment a
# node hangs or shuts down, so its last line under-reports; the engine's own
# "Finalized height N" log is written per finalization and never goes stale.
# Take whichever of the two is further along.
finalized_height() {
    local log="$WORK/node-$1.log"
    [ -f "$log" ] || return 0
    local sampled logged
    sampled="$(grep 'committee node=' "$log" 2>/dev/null \
        | tail -1 | sed -n 's/.*finalized=\([0-9]*\).*/\1/p')"
    logged="$(sed -n 's/.*\[Shadow\] Finalized height \([0-9]*\) .*/\1/p' "$log" 2>/dev/null | tail -1)"
    printf '%s\n' "$sampled" "$logged" | grep -E '^[0-9]+$' | sort -n | tail -1
}

# Per-node picture of the last reported counters — the first thing worth seeing
# whether the run passed or failed.
summary() {
    printf '\n%-5s %-6s %-7s %-6s %-6s %-9s %-9s %s\n' \
        node conns peers props votes timeouts certs finalized
    for index in $(seq 0 $((NODE_COUNT - 1))); do
        local line
        # NOT tail -1: the last sample is taken while the node is already tearing
        # down, so it reports conns=0 and reads as an isolated node. Take the last
        # line that still had peers — that is the working state we want to see.
        line="$(grep 'committee node=' "$WORK/node-$index.log" 2>/dev/null \
                | grep -v 'conns=0 ' | tail -1)"
        [ -n "$line" ] || line="$(grep 'committee node=' "$WORK/node-$index.log" 2>/dev/null | tail -1)"
        [ -n "$line" ] || { printf '%-5s (no counters reported)\n' "$index"; continue; }
        printf '%-5s %-6s %-7s %-6s %-6s %-9s %-9s %s\n' "$index" \
            "$(sed -n 's/.*conns=\([0-9-]*\).*/\1/p'         <<<"$line")" \
            "$(sed -n 's/.*shadow_peers=\([0-9]*\).*/\1/p'   <<<"$line")" \
            "$(sed -n 's/.*proposals=\([0-9]*\).*/\1/p'      <<<"$line")" \
            "$(sed -n 's/.*votes=\([0-9]*\).*/\1/p'          <<<"$line")" \
            "$(sed -n 's/.*timeouts=\([0-9]*\).*/\1/p'       <<<"$line")" \
            "$(sed -n 's/.*certificates=\([0-9]*\).*/\1/p'   <<<"$line")" \
            "$(finalized_height "$index")"
    done
    printf '\n'
    local roots
    roots="$(grep -ho 'rejected with error [0-9]*' "$WORK"/node-*.log 2>/dev/null | sort | uniq -c | sort -rn)"
    [ -n "$roots" ] && printf 'proposal rejections:\n%s\n\n' "$roots"
    return 0
}

[ -x "$NODE_RUN" ] || fail "node runner is absent: $NODE_RUN"
[ -x "$BUNDLE" ]   || fail "Shadow bundle tool is absent: $BUNDLE"
[ -x "$DAG_AUDIT" ] || fail "DAG audit tool is absent: $DAG_AUDIT"
[ -f "$SHADOW_VERIFY" ] || fail "cross-node verifier is absent: $SHADOW_VERIFY"
[ -d "$SEED/dag" ] || fail "seed DAG is absent: $SEED"
[ "$SENDERS" -ge 1 ] && [ "$SENDERS" -le "$NODE_COUNT" ] || fail "SENDERS must be 1..$NODE_COUNT"
[ "$PER_SENDER" -le 64 ] || fail "PER_SENDER above 64 hits maximum_nonce_gap; use more senders instead"

if command -v lsof >/dev/null 2>&1; then
    for port in $(seq "$BASE_PORT" $((BASE_PORT + 40))); do
        lsof -ti "tcp:$port" >/dev/null 2>&1 && fail "port $port is still busy"
    done
fi

log "=== bootstrap $NODE_COUNT nodes from $SEED ==="
EXTRACHAIN_TEST_BUILD="$BUILD_DIR" \
EXTRACHAIN_TEST_WORK="$SYNC_WORK" \
EXTRACHAIN_TEST_DFS_BYTES=1048576 \
    bash "$CORE_TESTS/multi_console_sync.sh" "$SEED" 6 "$BASE_PORT" >"$WORK/bootstrap.log" 2>&1 \
    || { tail -30 "$WORK/bootstrap.log" >&2; fail "DAG and ExDFS bootstrap failed"; }

# multi_console_sync's cleanup kills its nodes but does not wait for them, and the
# seed's data directory is still open while the server shuts down. Running the
# ceremony's --prepare on it in that window fails (1 in ~170 cycles), so wait
# for the server to be gone first.
for _ in $(seq 1 30); do
    ps -eo args --no-headers | grep -q "[e]xtrachain-node-run serve data $BASE_PORT " || break
    sleep 1
done

NODE_HOMES=("$SYNC_WORK/server/data")
for index in $(seq 1 6); do NODE_HOMES+=("$SYNC_WORK/client$index/data"); done

log "=== Shadow ceremony ==="
PREPARED=""
for index in $(seq 0 $((NODE_COUNT - 1))); do
    role="joiner"; [ "$index" -eq 0 ] && role="seed"
    prepared="$("$BUNDLE" --prepare "${NODE_HOMES[$index]}" "$role" 2>&1)" \
        || { printf '%s\n' "$prepared" >&2; fail "transition preparation failed for node $index"; }
    marker="$(sed -n 's/.*prepared network=\([^ ]*\) boundary=\([^ ]*\).*/\1:\2/p' <<<"$prepared")"
    [ -n "$marker" ] || fail "node $index did not report its transition boundary"
    if [ -z "$PREPARED" ]; then PREPARED="$marker"
    elif [ "$PREPARED" != "$marker" ]; then fail "node $index has a different transition boundary"; fi
done
"$BUNDLE" "${NODE_HOMES[0]}" "${NODE_HOMES[@]}" >"$WORK/bundle.log" 2>&1 \
    || { tail -40 "$WORK/bundle.log" >&2; fail "Shadow bundle creation failed"; }
log "boundary=$PREPARED leader=$(sed -n 's/.*leader_index=\([0-6]\).*/\1/p' "$WORK/bundle.log")"

# Spreading intents keeps every sender under maximum_sender_intents, and gives the
# state machine several independent nonce sequences to interleave — which is the
# condition the state_commitment defect is expected to need.
TOTAL_INTENTS=$((SENDERS * PER_SENDER))
log "=== committee: $SENDERS senders x $PER_SENDER intents = $TOTAL_INTENTS ==="
mkdir -p "$BARRIER"
# In the seed only node 0's actor holds funds, so with several senders node 0
# must first transfer to the other senders' actors; EXC_FUND_NODES drives the
# funding phase inside node-run (node 0 funds, listed senders wait for the
# "funded" barrier marker before submitting their own load).
FUND_NODES=""
if [ "${EXC_SHADOW_FUND:-1}" = "1" ] && [ "$SENDERS" -gt 1 ]; then
    FUND_NODES="$(seq 1 $((SENDERS - 1)) | paste -sd, -)"
    log "funding phase: node 0 pre-funds nodes $FUND_NODES"
elif [ "$SENDERS" -gt 1 ]; then
    log "negative mode: senders 1..$((SENDERS - 1)) stay unfunded, expecting eviction"
fi
for index in $(seq 0 $((NODE_COUNT - 1))); do
    if [ "$index" -eq 0 ]; then parent="$SYNC_WORK/server"; role="seed"
    else parent="$SYNC_WORK/client$index"; role="joiner"; fi
    intents=0
    [ "$index" -lt "$SENDERS" ] && intents="$PER_SENDER"
    port=$((BASE_PORT + 20 + index))
    (
        cd "$parent" || exit 73
        EXC_DEBUG_LOG=1 EXC_BIND_IP="127.0.0.$((index + 1))" EXC_FUND_NODES="$FUND_NODES" \
            exec "$NODE_RUN" committee data "$role" "$index" "$port" "$((BASE_PORT + 20))" "$NODE_COUNT" \
                 "$intents" "$RUN_SECONDS" "$BARRIER" 1 1
    ) >"$WORK/node-$index.log" 2>&1 &
    PIDS+=("$!")
    sleep 1
done

barrier_deadline=$(( $(date +%s) + 75 ))
while [ "$(find "$BARRIER" -maxdepth 1 -type f -name 'node-*' | wc -l)" -ne "$NODE_COUNT" ]; do
    [ "$(date +%s)" -ge "$barrier_deadline" ] && fail "the committee did not reach the start barrier"
    sleep 1
done
touch "$BARRIER/go"
log "committee started, deadline ${DEADLINE_S}s"

# Watch instead of blocking on wait(): the interesting outcomes (all intents
# finalized / InvalidRoot / voting stalled) are all visible while the nodes run.
deadline=$(( $(date +%s) + DEADLINE_S ))
verdict=""
while :; do
    done_nodes=0
    for index in $(seq 0 $((SENDERS - 1))); do
        grep -q "finalized intents=$PER_SENDER" "$WORK/node-$index.log" 2>/dev/null && done_nodes=$((done_nodes + 1))
    done
    if [ "$done_nodes" -eq "$SENDERS" ]; then verdict="pass"; break; fi
    # Chaos kills: a run passes on the survivors when every sender that has not
    # finalized is dead and no more than ALLOWED_DEAD of them died.
    if [ "$ALLOWED_DEAD" -gt 0 ]; then
        dead_senders=0; unfinished_alive=0
        for index in $(seq 0 $((SENDERS - 1))); do
            grep -q "finalized intents=$PER_SENDER" "$WORK/node-$index.log" 2>/dev/null && continue
            if kill -0 "${PIDS[$index]}" 2>/dev/null; then unfinished_alive=1; else dead_senders=$((dead_senders + 1)); fi
        done
        if [ "$unfinished_alive" -eq 0 ] && [ "$dead_senders" -le "$ALLOWED_DEAD" ]; then verdict="pass"; break; fi
    fi
    # Negative mode: unfunded senders can never finalize; the pass condition is
    # that node 0 finishes its load anyway and the poison intents were evicted.
    if [ "${EXC_SHADOW_FUND:-1}" != "1" ] && [ "$SENDERS" -gt 1 ]; then
        evictions="$(grep -ch "Evicted .* unprovable intents" "$WORK"/node-*.log 2>/dev/null \
                     | awk '{s+=$1} END {print s+0}')"
        if grep -q "finalized intents=$PER_SENDER" "$WORK/node-0.log" 2>/dev/null \
           && [ "$evictions" -ge 1 ]; then verdict="pass-negative"; break; fi
    fi

    # A single InvalidRoot could still be transient, so give the network a grace
    # period to recover from it. Bailing on the first one would hide whether the
    # rejection loop is self-healing — which is exactly what we need to know.
    rejects="$(grep -ch "rejected with error 11\|Leader batch validation failed: 11" "$WORK"/node-*.log 2>/dev/null \
               | awk '{s+=$1} END {print s+0}')"
    if [ "$rejects" -ge 50 ]; then verdict="invalid-root"; break; fi
    alive=0
    for pid in "${PIDS[@]}"; do kill -0 "$pid" 2>/dev/null && alive=$((alive + 1)); done
    if [ "$alive" -eq 0 ]; then verdict="exited"; break; fi
    if [ "$(date +%s)" -ge "$deadline" ]; then verdict="deadline"; break; fi
    sleep 5
done

for index in $(seq 0 $((NODE_COUNT - 1))); do
    kill -0 "${PIDS[$index]}" 2>/dev/null || DEAD_NODES="$DEAD_NODES$index "
done
if [ -n "$DEAD_NODES" ]; then
    if [ "$(printf '%s' "$DEAD_NODES" | wc -w)" -le "$ALLOWED_DEAD" ]; then
        log "note: node(s) $DEAD_NODES died during the run (allowed); the survivors are audited"
    else
        log "node(s) $DEAD_NODES died during the run"
    fi
fi

# A receipt proves that the submitting node applied the checkpoint. Other nodes
# can still be importing the same certified height. Keep the committee alive
# until every node reports the seed node's finalized count, so the audits test a
# converged snapshot instead of a shutdown race.
if [ "$verdict" = "pass" ] || [ "$verdict" = "pass-negative" ]; then
    convergence_deadline=$(( $(date +%s) + 60 ))
    while :; do
        # Every surviving node has to report one finalized count; the first
        # survivor (the seed, unless it died) is the reference.
        reference=""
        converged=1
        for index in $(seq 0 $((NODE_COUNT - 1))); do
            is_dead "$index" && continue
            node_finalized="$(finalized_height "$index")"
            [ -n "$node_finalized" ] || { converged=0; continue; }
            [ -n "$reference" ] || reference="$node_finalized"
            [ "$node_finalized" = "$reference" ] || converged=0
        done
        [ -n "$reference" ] || converged=0
        if [ "$converged" -eq 1 ]; then
            # Heights agree; the ExDFS mesh has to be complete as well before the
            # audits read a snapshot.
            dfs_audit 0 && break
        fi
        if [ "$(date +%s)" -ge "$convergence_deadline" ]; then
            # Name the thing that is actually still missing at the deadline.
            verdict="convergence"
            [ "$converged" -eq 1 ] && verdict="dfs-incomplete"
            break
        fi
        sleep 1
    done
fi

# Optional hold: keep the converged committee serving for a while so an outside
# node can join it live (see shadow_live_join.sh). On a fast host the whole load
# finalizes in seconds, so without this there is nothing left to join.
if [ "$verdict" = "pass" ] && [ "${EXC_SHADOW_HOLD_S:-0}" -gt 0 ]; then
    log "holding the committee for ${EXC_SHADOW_HOLD_S}s"
    sleep "$EXC_SHADOW_HOLD_S"
fi

# A stack from a live node is worth more than the same node killed — this is how
# the ABBA deadlock was found. Take it before the processes go away.
# Not for dfs-incomplete: the committee is healthy there, and attaching gdb to
# seven nodes stops them for minutes — the counters then show a stall that the
# sampler itself caused.
if [ "$verdict" != "pass" ] && [ "$verdict" != "pass-negative" ] && [ "$verdict" != "dfs-incomplete" ]; then
    if command -v sample >/dev/null 2>&1; then
        for pid in "${PIDS[@]}"; do
            kill -0 "$pid" 2>/dev/null && sample "$pid" 3 -f "$WORK/sample-$pid.txt" >/dev/null 2>&1 &
        done
        wait
    elif command -v gdb >/dev/null 2>&1; then
        for pid in "${PIDS[@]}"; do
            kill -0 "$pid" 2>/dev/null \
                && gdb -batch -p "$pid" -ex "thread apply all bt" > "$WORK/sample-$pid.txt" 2>&1 &
        done
        wait
    else
        printf 'no stack sampler (sample/gdb) on this host: %s\n' "$WORK" >&2
    fi
fi
cleanup

if [ "$verdict" = "pass" ] || [ "$verdict" = "pass-negative" ]; then
    # Nodes are killed one after another, so the last one alive can still commit a
    # checkpoint or two. Comparing snapshot POSITIONS across nodes therefore fails on
    # a perfectly good run. What must agree is the CONTENT at a shared section: group
    # the snapshots by section and require one hash per section.
    declare -A SNAPSHOT_HASH=() SNAPSHOT_OWNER=()
    for index in $(seq 0 $((NODE_COUNT - 1))); do
        role="joiner"; [ "$index" -eq 0 ] && role="seed"
        "$DAG_AUDIT" "${NODE_HOMES[$index]}" "$role" >"$WORK/audit-$index.log" 2>&1 \
            || { tail -60 "$WORK/audit-$index.log" >&2; fail "DAG or balance audit failed for node $index"; }
        snapshot="$(sed -n 's/.*balance cache: section=\([^ ]*\).*hash=\([^ ]*\).*/\1:\2/p' \
                    "$WORK/audit-$index.log")"
        [ -n "$snapshot" ] || fail "node $index did not report a balance snapshot"
        section="${snapshot%%:*}"
        hash="${snapshot#*:}"
        if [ -n "${SNAPSHOT_HASH[$section]:-}" ] && [ "${SNAPSHOT_HASH[$section]}" != "$hash" ]; then
            printf 'node %s and node %s disagree at section %s: %s vs %s\n' \
                "${SNAPSHOT_OWNER[$section]}" "$index" "$section" \
                "${SNAPSHOT_HASH[$section]}" "$hash" >&2
            fail "nodes disagree on the balance snapshot at section $section"
        fi
        SNAPSHOT_HASH[$section]="$hash"
        SNAPSHOT_OWNER[$section]="$index"
    done
    if [ "${#SNAPSHOT_HASH[@]}" -gt 1 ]; then
        log "note: nodes stopped at ${#SNAPSHOT_HASH[@]} different snapshot sections (shutdown skew, not a mismatch)"
    fi
    EXC_VERIFY_SKIP="$DEAD_NODES" python3 "$SHADOW_VERIFY" "$WORK" >"$WORK/cross-node.log" 2>&1 \
        || { tail -80 "$WORK/cross-node.log" >&2; fail "cross-node content verification failed"; }
fi

case "$verdict" in
    pass)
        summary
        [ "$DFS_BYTES" -gt 0 ] && dfs_audit 1
        log "PASS: $SENDERS senders finalized $PER_SENDER intents each ($TOTAL_INTENTS total)"
        printf 'stand data: %s\n' "$WORK"
        ;;
    pass-negative)
        summary
        log "PASS (negative): node 0 finalized $PER_SENDER intents while unfunded senders were evicted"
        grep -h "Evicted .* unprovable intents" "$WORK"/node-*.log 2>/dev/null | head -3
        printf 'stand data: %s\n' "$WORK"
        ;;
    invalid-root)
        printf '\n--- InvalidRoot evidence ---\n' >&2
        grep -h "rejected with error 11" "$WORK"/node-*.log 2>/dev/null | head -5 >&2
        fail "proposals rejected with InvalidRoot (11) — state_commitment divergence"
        ;;
    exited)
        # Nodes leaving on schedule is not a crash — it means finality never came
        # within their window. Say which one it was, they need different fixes.
        fail "nodes finished their ${RUN_SECONDS}s window with $done_nodes/$SENDERS senders finalized" ;;
    deadline) fail "harness deadline reached with $done_nodes/$SENDERS senders finalized" ;;
    convergence) fail "committee did not converge on one finalized height within 60s" ;;
    dfs-incomplete)
        dfs_audit 1 >&2
        fail "ExDFS content did not reach every node within 60s of finality" ;;
esac
