# AGENTS.md

Instructions for coding agents working in this repository live in **[CLAUDE.md](CLAUDE.md)**.

Read it before making changes: it covers the architecture map, build and formatting rules,
the subsystems most often touched, and the commit conventions (one tag, one line, no
trailers, no merges from a work branch).

Testing expectations are in **[docs/TESTING.md](docs/TESTING.md)** — in short: any change
to the core is validated on a combined DAG + DFS stand, both at once.

Before running a stand, read **[docs/STAND.md](docs/STAND.md)** as well. Use
`scripts/stand.py` to prepare and run the validation sequence. Declare each run in a
profile instead of creating a new supervisor script for each revision. Keep full logs
on the stand host; use compact status, the final report, and bounded diagnostics.
Do not use repeated model calls to wait for completion. Acceptance and retention rules
are defined in `docs/TESTING.md` and `docs/STAND.md`.

Open work is tracked in **[docs/TODO.md](docs/TODO.md)**.
