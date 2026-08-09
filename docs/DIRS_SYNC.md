# Dirs & vector distribution — design

How `.dirs` (the DFS metadata catalogue) and the payloads of vectors and dictionaries are
supposed to reach a node. Written against the state of `pre-dev-dfs`; the "what exists
today" sections are read off the code, not remembered.

The problem this answers: a node that was busy or offline at the wrong moment ends up
**permanently** without a vector — either without its dir row, or with the row and no
payload — and nothing ever corrects it. Vectors carry chats, profiles, usernames and
lists, so "permanently missing" is not a cosmetic defect. See `TODO.md` §0.4.

---

## 1. The model

**Default behaviour is the ordinary one. The special behaviour is requested explicitly by
the messenger.** The core never assumes a UI exists: a node with no client attached, a
test stand node, a server node — all of them just work, completely, without anyone
telling them to.

### light — the default, always

A node pulls the **whole `.dirs` catalogue** — metadata only, never payloads — and keeps
it complete. Payloads are then fetched lazily, on demand or in the background.

This is the base, and it is not negotiable: **without a complete catalogue a node cannot
know what it is missing.** Repair, catch-up, reconciliation and the test harness all need
something to compare local state against. A node holding a partial catalogue does not
have a smaller problem than a node holding none — it has a *silent* one, because nothing
in the system can tell "I do not have it" apart from "it does not exist".

It is also what makes the stand honest: the vector audit measures convergence against
what a node *knows* it should hold. With an opportunistic catalogue the audit would
measure its own blindness.

### high-light — a priority overtake, not a mode

When something is needed *now* — the user opened a chat — the messenger asks for that
specific vector: its dir row plus its content, ahead of any background work.

Three properties define it:

1. **It is requested by the client, not decided by the core.** The core exposes the
   mechanism; the messenger knows which chat is on screen.
2. **It overtakes, it does not replace.** The background pull of the full catalogue keeps
   running and is never cancelled by an urgent request.
3. **The full catalogue follows.** Fetch what is needed first, then still request the
   full `.dirs`.

The natural trigger is the moment the UI's "please wait, downloading" indicator
disappears: the urgent item has landed, the user is unblocked, and pulling the rest now
costs nothing perceptible.

### Why the order matters

The inverse — fetch only what was asked for, and the full catalogue "when needed" — is
exactly the state we are leaving. A node that only ever learns about vectors it already
asked for cannot discover the ones it never heard of, and no catch-up mechanism helps,
because catch-up needs a reference to diff against.

### The UI trigger is an optimisation, not the guarantee

"The messenger requests the full catalogue when the spinner disappears" is the *fast
path*. It must not be the only path. The app can be killed before the spinner clears, or
crash, or the user can navigate away — and then the catalogue is never requested at all
and the node stays in precisely the broken state this design exists to remove.

So: **the core owns a fallback.** If the catalogue is incomplete and no urgent work is
pending, the core completes it on its own initiative. The UI trigger then means "now is a
good moment", not "otherwise never". A design where correctness depends on a client
sending a message at the right time is not a design, it is a hope.

---

## 2. What exists today

Worth reading before building anything — a surprising amount of the machinery is already
there, and one crucial piece is switched off.

| Piece | Where | State |
|---|---|---|
| `DfsMode::{Full, Light}` | `headers/utils/exc_utils.h:71` | exists; Light filters sync responses to `startup_sync_actors()` |
| Priority actors / file links | `dfs_controller.h:130-205` | exists — `add_priority_actor`, `add_priority_file_link`, `is_priority`; used to sort sync responses and admit downloads |
| Staged startup sync | `dfs_controller.cpp:2774` `refresh_actors` | exists — asks specific actors, falls back to a full dump after 3s if the peer does not answer |
| Full catalogue dump | `dirs_manager.cpp` `temp_sync_all` / `network_request_all` | exists and is the *only* live path |
| **Targeted dir-row request** | `dirs_manager.cpp:248` `network_request_dir_rows` | **written, then disabled by an unconditional `return;` on the first line** |
| Incremental fetch by watermark | `network_request_all` | **absent** — `get_dir_rows(db_, actor, 0)` always reads from zero, even for a staged request |
| Vector content request | `dfs_controller.cpp:1388` `request_vector_content` | exists, throttled separately |

Two consequences follow directly, and together they *are* the §0.4 defect:

- **There is no way to ask for what you are missing.** `DfsSyncDirRows` has a sender and a
  receiver, but the receiver returns immediately, so the only question a node can ask is
  "give me everything for these actors". Nothing can say "give me rows newer than X" or
  "give me this one row".
- **Nothing is incremental.** Even the staged path re-reads every row from timestamp zero,
  so "complete the catalogue" and "re-download the catalogue" are the same operation, and
  the expensive one.

That is why the missing row is permanent: the creation broadcast is the only cheap way to
learn a row exists, and if it is missed, the alternative is a full dump nobody triggers.

---

## 3. The pieces to build

Ordered by dependency. Each is independently testable.

### 3.1 Re-enable targeted dir-row requests, with a watermark

Remove the `return;` in `network_request_dir_rows` and make the request carry a
watermark: *give me rows for actor A modified after T*. The responder side already
accepts `last_modified` in `get_dir_rows`; the caller must stop passing `0`.

This turns "complete my catalogue" into a cheap, repeatable operation instead of a full
dump, which is what makes every mechanism below affordable.

Note before re-enabling: the `return;` was added in `c44121ea` (2025-05-17), in a commit
that says only "Some improves" — so the reason is not recorded anywhere. An unconditional
`return` at the top of a handler is usually someone stopping a fire, and the fire may
still be there. Recall or reconstruct why before switching it back on, and treat a
response storm or a request loop as the expected hazard.

### 3.2 A catalogue completeness check the core runs itself

The core needs to answer "is my catalogue complete?" without a client. The cheap version:
per owner, compare a digest (row count + max `last_modified`) with a peer; on mismatch,
fetch rows above the local watermark via 3.1.

This is the fallback from §1: when nothing urgent is pending, reconcile in the background,
at a low rate. It is what makes the design correct rather than hopeful.

### 3.3 An explicit urgent API for the messenger

Something the client calls, in plain terms: "I need this vector now."

The core then fetches the dir row (if absent) and the content ahead of the queue, and —
regardless of how that ends — leaves the background catalogue completion running. The
existing `add_priority_file_link` is the natural seat for this; what it lacks is fetching
a dir row the node does not have, and a completion signal the client can wait on.

### 3.4 A "catalogue complete" signal, and the UI trigger

The messenger asks for the full catalogue when the spinner disappears. In the core this
is simply an entry point meaning "background completion may go at full speed now"; it
must be safe to call twice, and safe never to call at all (3.2 covers that case).

### 3.5 Payload repair for known rows

Already partly in place on `pre-dev-dfs`: a Full node that learns a vector row without the
payload re-requests the content, and the download queue accepts a vector whose payload is
missing locally rather than only an outdated one. Keep both — with 3.1 and 3.2 they stop
being the only line of defence and become what they should be: a safety net.

---

## 4. Invariants to hold

1. A node with no client attached must converge to a complete catalogue on its own.
2. An urgent request never cancels or postpones background completion indefinitely.
3. Completing a catalogue costs proportional to what is missing, not to its total size.
4. "Row known, payload missing" and "row unknown" must both self-heal; neither may be a
   terminal state.
5. Light mode restricts *which* actors a node cares about — never whether its catalogue
   for those actors is complete.

---

## 5. How this gets tested

Per `TESTING.md`, on the combined DAG + DFS stand, both load profiles.

The audit already in place is the right one: for every `type=30` row across all nodes,
check the payload physically exists — not the dir row, the file. Add to it:

- a node started late into a live network must reach a complete catalogue with no client
  and no urgent request (invariant 1);
- a node that misses a creation broadcast (frozen with `SIGSTOP` during creation, resumed
  after) must recover the vector (invariant 4);
- the byte volume of catalogue completion must scale with the gap, not with the catalogue
  (invariant 3) — a re-sync of a node missing three rows must not move megabytes.
