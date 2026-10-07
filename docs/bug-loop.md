# Bug loop

`tools/bugs/bug_loop.py` works the central bug API (`/api/bugs`, fed by the in-game bug
reporter) like a backlog: it triages each new report, fixes real defects with a regression
test, and auto-ships low-risk fixes to `origin/develop`. Everything else is parked on a
`bugfix/<id8>` branch for review. Design: `docs/superpowers/specs/2026-10-07-bug-loop-design.md`.

## Stages

| Stage | Runs as | Can |
|---|---|---|
| Triage | `claude -p --tools ""` | classify only; reads the raw report |
| Fix | `claude -p` in `H:/mmo-bugloop` | edit, build, test, commit on `bugfix/<id8>` |
| Review | `claude -p --tools Read,Grep,Glob` | read the branch; never sees the report |
| Guard | `bugloop/guard.py` | protected paths, suspicious patterns, data field rules |
| Proof | orchestrator | test fails on base, passes on the fix |
| Gate | `verify.ps1 -Tier full` | build, unit, tool tests, E2E |

The guard is an allow-list: only paths under these roots (each with its own file-suffix list,
`ROOT_SUFFIXES`) can auto-ship; everything else parks.

- `src/shared/{game,game_server,game_client,frame_ui,scene_graph,graphics,graphics_d3d11,graphics_metal,terrain,deferred_shading,audio,math,client_data}/`
- `src/world_server/`, `src/realm_server/`, `src/mmo_client/` (C/C++ files)
- `data/client/Interface/` (`.lua`, `.xml`, `.toc`), `data/client/Locales/` (`.txt`)
- `data/editor/data/` (`.data`), `data/scripts/` (`.lua`)

The guard also parks: edits within 20 lines of a security-sensitive identifier or a rejection
path; changed server-code lines containing economy words (xp, reward, loot, drop, credit,
price, cost, ...) even without numbers; numeric literals in conditions or tuned values;
added or removed preprocessor directives (including `#include`); Lua `require`, `dofile` and
`loadstring`; an added `return true;` or `return PacketParseResult::...` in server code;
control characters; unparseable diff headers; and submodule pointer changes.

The regression-proof waiver is decided by the diff, not the fixer's claim: a fix may skip the
regression test only if all of its non-test changes are under `data/editor/data/`,
`data/client/Locales/` or `data/client/Interface/`.

A fix auto-ships only with: a fixable category, fixer confidence `high`, a clean review, an
empty guard, a passing proof (code fixes), a green full gate, fewer than 5 ships today, no
breaker, and a time outside 21:30–23:59 UTC (fixes ready during the window ship after it).

## Running it

Prerequisites:

- The standalone `claude` CLI must be logged in for the user account the scheduled task runs
  as: run `claude` once and `/login` (the desktop app's login does not cover it).
- Run `tools/bugs/adversarial_check.py` once; it must print 8 `ok` lines. Do this before the
  shadow day.
- `MMO_BUG_API_KEY` and `MMO_E2E_MYSQL_PASSWORD` as user environment variables.
- Git push access to origin (ssh) and HTTPS access to the data submodule remotes.

```powershell
$env:MMO_BUG_API_KEY = "<reader key>"
& $env:MMO_GATE_PYTHON tools/bugs/bug_loop.py watch --dry-run --once   # one shadow poll
& $env:MMO_GATE_PYTHON tools/bugs/bug_loop.py watch --dry-run          # shadow mode
powershell -File tools/bugs/register_bug_loop_task.ps1                 # live, at logon
```

The scheduled task runs a snapshot of `origin/develop` (`H:/mmo-bugloop-runtime`), so loop
changes only take effect once they are pushed to develop. It restarts itself every UTC day.

Stale claims: if the loop is killed mid-fix, the next live poll releases the bug as `triaged`
with an "interrupted" note.

## Where to look

- `tools/gate/reports/bugloop-YYYY-MM-DD.json`: the day's outcomes, ships, parked branches,
  abuse flags, model invocations (`bugloop-dry-*.json` in shadow mode).
- `artifacts/bug-loop/<bug id>/`: `report.json`, `triage.json`, `FIX.json`, `diff.patch`,
  `guard.json`, `review.json`, `proof.json`, `gate.json`, `decision.json`.
- `artifacts/bug-loop/_runs/<date>.log`: the runner log. `dry-run-journal.jsonl`: the API
  writes a shadow run would have made.
- The bug's history in the web UI (`actor: bug-loop`). Abuse flags: web UI → Flagged reporters.

## Reviewing a parked fix

Parked fixes are expected and normal: measured on 761 small historical server edits, the guard
parks about 49%. They wait for review. The note on the bug names every reason it parked. Check out `bugfix/<id8>`, review it, and
`/ship` it. The loop notices the merge and marks the bug resolved. To reject it, set the bug
to `wontfix` and delete the branch.

## Circuit breaker

`artifacts/bug-loop/BREAKER` stops all auto-shipping; parking continues. It trips by itself
when a nightly report is red and lists a `Merge bugfix/...` among `merges_since_last_green`.
After a deploy rollback, trip it by hand. Only clear it once the cause is understood:

```powershell
& $env:MMO_GATE_PYTHON tools/bugs/bug_loop.py breaker on --reason "rollback of nightly-..."
& $env:MMO_GATE_PYTHON tools/bugs/bug_loop.py breaker off
```

## Known limits

- Numeric changes in economy data (items, quests, loot, units, spells and so on) always park.
  So do binary assets and files that cannot be decoded.
- `items.data` stores field 68 with a wire type that differs from `items.proto` (38 entries).
  The data diff surfaces such unknown fields as `<unknown N>` leaves (changed ones count as
  numeric), so item text fixes still work.
- A fix in `data/client` or `data/editor` ships only if the submodule's `master` on GitHub has
  not moved past the commit the fix started from; otherwise it parks.
- After an auto-ship, the local `develop` in `H:/mmo` is behind `origin/develop`. Pull before
  pushing your own work.
- After changing `prompts/triage.md`, run `tools/bugs/adversarial_check.py` (real model calls).

### What the guard cannot see

- Logic inversions that use neutral tokens.
- Edits to helpers or stat formulas far from any sensitive token.
- Lua names built at runtime.
- Reward changes that use neutral names and no literals.

The review stage and the gate are the remaining layers for these.
