#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SPEC="$ROOT_DIR/spec/consensus/identity_bft_shadow.qnt"
QUINT=(npx --yes @informalsystems/quint@0.32.0)

"${QUINT[@]}" typecheck "$SPEC"

for invariant in \
    noConflictingFinality \
    noHonestDoubleVote \
    noDurableDoubleVote \
    restartRestoresDurableVote \
    lockDoesNotConflictWithDurableVote \
    roundChangeRequiresQuorum \
    voteRequiresAvailableData \
    epochActivationIsTwoStep \
    epochActivationRequiresFinality; do
    "${QUINT[@]}" run "$SPEC" \
        --main=shadow_consensus \
        --invariant="$invariant" \
        --max-steps=40 \
        --max-samples=100000 \
        --verbosity=0
done
