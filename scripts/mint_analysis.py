#!/usr/bin/env python3
"""
Mint abuse analysis script.

Scans DAG sections from a05133 (hex) to current section and reports:
- Who received minted tokens (Mint transactions)
- How much they spent (Regular transactions from minted actors+tokens)
- Potential abuse: spending minted tokens before the freeze was added

Usage:
    python3 scripts/mint_analysis.py <dag_folder>

Example:
    python3 scripts/mint_analysis.py ~/.local/share/ExtraChain/dag

The script reads sections from disk (folder/{section_id/10000}/{section_id})
and outputs a report to stdout.
"""

import os
import sys
import json
from pathlib import Path
from decimal import Decimal, InvalidOperation
from collections import defaultdict
from datetime import datetime, timezone


# ── Constants (mirror dag.cpp) ──────────────────────────────────────────────

MIN_SECTION_HEX = "a05133"
MIN_SECTION = int(MIN_SECTION_HEX, 16)  # 656691
SECTION_SIZE = 10000                     # Config::DataStorage::SECTION_SIZE

# Minting owner/token from dag.cpp:1029-1030
MINTING_OWNER = "46710a2d823c23db9fc2ac01e0f84212a8128373"
MINTING_TOKEN = "468faf2f1be6504a9a26f7f027f7e43380b0d77d"

# Transaction types (TransactionType enum from transaction.h)
TX_MINT    = 7  # Minting
TX_REGULAR = 1  # Regular

ZERO = Decimal("0")


# ── Section reading ──────────────────────────────────────────────────────────

def parse_name(name: str) -> int | None:
    """Parse a file/folder name as decimal or hex integer."""
    try:
        return int(name)
    except ValueError:
        pass
    try:
        return int(name, 16)
    except ValueError:
        pass
    return None


def read_section(dag_dir: Path, section_id: int) -> dict | None:
    # Try both decimal and hex names for folder and file
    folder_dec = str(section_id // SECTION_SIZE)
    folder_hex = format(section_id // SECTION_SIZE, "x")
    file_dec   = str(section_id)
    file_hex   = format(section_id, "x")

    for folder_name in (folder_dec, folder_hex):
        for file_name in (file_dec, file_hex):
            p = dag_dir / folder_name / file_name
            if p.exists():
                try:
                    with open(p, "r", encoding="utf-8") as f:
                        return json.load(f)
                except Exception:
                    return None
    return None


def find_current_section(dag_dir: Path) -> int:
    """Return the highest section ID found on disk (supports hex and decimal names)."""
    max_id = 0
    if not dag_dir.exists():
        return max_id
    for folder in dag_dir.iterdir():
        if not folder.is_dir():
            continue
        if parse_name(folder.name) is None:
            continue
        for f in folder.iterdir():
            if not f.is_file():
                continue
            sid = parse_name(f.name)
            if sid is not None and sid > max_id:
                max_id = sid
    return max_id


# ── Decimal helpers ──────────────────────────────────────────────────────────

def hex_to_dec_int(h: str) -> str:
    """Convert hex integer string to decimal string."""
    if not h:
        return "0"
    negative = h.startswith("-")
    if negative:
        h = h[1:]
    result = str(int(h, 16)) if h else "0"
    return f"-{result}" if negative else result


def parse_amount(s) -> Decimal:
    """Parse decimal or hex BigNumberFloat string."""
    if s is None or s == "":
        return ZERO
    s = str(s)
    try:
        # Try decimal first
        return Decimal(s)
    except InvalidOperation:
        pass
    # Hex float: "1a.4f" or "1a"
    try:
        negative = s.startswith("-")
        if negative:
            s = s[1:]
        if "." in s:
            int_part, frac_part = s.split(".", 1)
            zeros = len(frac_part) - len(frac_part.lstrip("0"))
            frac_stripped = frac_part.lstrip("0")
            int_dec  = hex_to_dec_int(int_part) if int_part else "0"
            frac_dec = hex_to_dec_int(frac_stripped) if frac_stripped else "0"
            dec_str  = f"{int_dec}.{'0' * zeros}{frac_dec}"
        else:
            dec_str = hex_to_dec_int(s)
        result = Decimal(dec_str)
        return -result if negative else result
    except Exception:
        return ZERO


def fmt_amount(d: Decimal) -> str:
    # Remove trailing zeros after decimal point
    s = f"{d:.10f}".rstrip("0").rstrip(".")
    return s if s else "0"


# ── Timestamp helpers ────────────────────────────────────────────────────────

def ms_to_dt(ms) -> str:
    try:
        ts = int(ms) / 1000.0
        return datetime.fromtimestamp(ts, tz=timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
    except Exception:
        return str(ms)


# ── Main analysis ────────────────────────────────────────────────────────────

def analyze(dag_dir: Path):
    print(f"DAG folder : {dag_dir}")
    print(f"Min section: {MIN_SECTION} (0x{MIN_SECTION_HEX})")

    current = find_current_section(dag_dir)
    if current == 0:
        print("ERROR: no sections found on disk")
        sys.exit(1)

    print(f"Current    : {current} (0x{current:x})")
    print(f"Range      : {MIN_SECTION} .. {current}  ({current - MIN_SECTION + 1} sections)")
    print()

    # actor+token -> total minted (from Mint txs)
    minted: dict[tuple[str, str], Decimal] = defaultdict(Decimal)
    # actor+token -> total spent (from Regular txs)
    spent: dict[tuple[str, str], Decimal] = defaultdict(Decimal)
    # list of all Regular transfers involving minted actors/tokens
    transfers: list[dict] = []
    # list of all Mint transactions
    mint_txs: list[dict] = []

    sections_read = 0
    sections_missing = 0

    for sid in range(MIN_SECTION, current + 1):
        section = read_section(dag_dir, sid)
        if section is None:
            sections_missing += 1
            continue
        sections_read += 1

        if sections_read % 10000 == 0:
            pct = (sid - MIN_SECTION) / max(current - MIN_SECTION, 1) * 100
            print(f"  [{pct:.1f}%] section {sid}  read={sections_read}  missing={sections_missing}",
                  flush=True)

        txs = section.get("transactions") or []
        for tx in txs:
            tx_type  = tx.get("type", "")
            token    = tx.get("token", "")
            sender   = tx.get("sender", tx.get("from", ""))
            receiver = tx.get("receiver", tx.get("to", ""))
            amount   = parse_amount(tx.get("amount", 0))
            ts       = tx.get("timestamp", 0)

            if tx_type == TX_MINT:
                key = (receiver, token)
                minted[key] += amount
                mint_txs.append({
                    "section": sid,
                    "actor": receiver,
                    "token": token,
                    "amount": amount,
                    "ts": ts,
                })

            elif tx_type == TX_REGULAR:
                # Track all Regular txs — we'll filter after collecting mint data
                transfers.append({
                    "section": sid,
                    "from": sender,
                    "to": receiver,
                    "token": token,
                    "amount": amount,
                    "ts": ts,
                })

    print(f"\nRead {sections_read} sections, {sections_missing} missing\n")

    # Build set of (actor, token) pairs that received mints
    minted_pairs = set(minted.keys())

    # ── Transitive chain tracking ─────────────────────────────────────────────
    # For each token: track all actors "tainted" by minted tokens (BFS, max depth 10)
    # tainted[token] = set of actors who received minted tokens directly or transitively
    tainted: dict[str, set[str]] = defaultdict(set)
    for actor, token in minted_pairs:
        tainted[token].add(actor)

    # Build index: (sender, token) -> list of transfers
    transfers_by_sender: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for t in transfers:
        transfers_by_sender[(t["from"], t["token"])].append(t)

    MAX_DEPTH = 10
    for token in list(tainted.keys()):
        queue = list(tainted[token])
        visited = set(queue)
        depth = 0
        while queue and depth < MAX_DEPTH:
            next_queue = []
            for actor in queue:
                for t in transfers_by_sender.get((actor, token), []):
                    receiver = t["to"]
                    if receiver not in visited:
                        visited.add(receiver)
                        tainted[token].add(receiver)
                        next_queue.append(receiver)
            queue = next_queue
            depth += 1

    # Collect all transfers in tainted chains
    chain_transfers = [t for t in transfers
                       if t["from"] in tainted.get(t["token"], set())
                       or t["to"] in tainted.get(t["token"], set())]

    # Filter direct: sender had minted tokens
    relevant_transfers = [t for t in transfers
                          if (t["from"], t["token"]) in minted_pairs]

    # Sum up spent per (actor, token)
    for t in relevant_transfers:
        key = (t["from"], t["token"])
        spent[key] += t["amount"]

    # ── Report ────────────────────────────────────────────────────────────────

    print("=" * 70)
    print("MINT TRANSACTIONS")
    print("=" * 70)
    if not mint_txs:
        print("  (none found)")
    else:
        for m in sorted(mint_txs, key=lambda x: x["section"]):
            print(f"  section={m['section']:>10}  {ms_to_dt(m['ts'])}")
            print(f"    actor ={m['actor']}")
            print(f"    token ={m['token']}")
            print(f"    amount={fmt_amount(m['amount'])}")

    print()
    print("=" * 70)
    print("SUMMARY PER ACTOR+TOKEN")
    print("=" * 70)

    abuse_count = 0
    for key in sorted(minted.keys()):
        actor, token = key
        mint_total   = minted[key]
        spent_total  = spent.get(key, ZERO)
        frozen       = max(mint_total - spent_total, ZERO)
        overspend    = max(spent_total - mint_total, ZERO)

        abused = spent_total > ZERO

        print(f"\n  actor  = {actor}")
        print(f"  token  = {token}")
        print(f"  minted = {fmt_amount(mint_total)}")
        print(f"  spent  = {fmt_amount(spent_total)}")
        print(f"  frozen = {fmt_amount(frozen)}")
        if overspend > ZERO:
            print(f"  OVERSPEND (spent more than minted!): {fmt_amount(overspend)}")
        if abused:
            abuse_count += 1
            print(f"  ** USED MINTED TOKENS BEFORE FREEZE **")

    print()
    print("=" * 70)
    print("TAINTED CHAIN (mint -> proxy -> ... -> collector)")
    print("=" * 70)
    if not chain_transfers:
        print("  (none)")
    else:
        # Show collector stats: how much each non-minted actor received from tainted senders
        collector_received: dict[tuple[str, str], Decimal] = defaultdict(Decimal)
        for t in chain_transfers:
            if (t["to"], t["token"]) not in minted_pairs:
                collector_received[(t["to"], t["token"])] += t["amount"]

        if collector_received:
            print("\n  -- Collectors (received tainted tokens, no direct mint) --")
            for (actor, token), total in sorted(collector_received.items(),
                                                 key=lambda x: -x[1]):
                print(f"  actor={actor}  token={token[:8]}...  received={fmt_amount(total)}")

        print("\n  -- All chain transfers (chronological) --")
        for t in sorted(chain_transfers, key=lambda x: x["section"]):
            from_minted = (t["from"], t["token"]) in minted_pairs
            to_minted   = (t["to"],   t["token"]) in minted_pairs
            tag = ""
            if from_minted and not to_minted:
                tag = " [MINT->COLLECTOR]"
            elif from_minted and to_minted:
                tag = " [MINT->MINT]"
            elif not from_minted:
                tag = " [CHAIN]"
            print(f"  section={t['section']:>10}  {ms_to_dt(t['ts'])}{tag}")
            print(f"    from  ={t['from']}")
            print(f"    to    ={t['to']}")
            print(f"    amount={fmt_amount(t['amount'])}")

    print()
    print("=" * 70)
    print(f"TOTAL: {len(minted)} minted actor+token pairs, "
          f"{abuse_count} used tokens before freeze, "
          f"{len(relevant_transfers)} direct transfers, "
          f"{len(chain_transfers)} chain transfers found")
    print("=" * 70)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    dag_dir = Path(sys.argv[1])
    if not dag_dir.exists():
        print(f"ERROR: folder not found: {dag_dir}")
        sys.exit(1)

    analyze(dag_dir)


if __name__ == "__main__":
    main()
