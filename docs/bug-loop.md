# Bug loop

`tools/bugs/bug_loop.py` works the central bug API (`/api/bugs`, fed by the in-game bug
reporter) like a backlog: it triages each new report, fixes real defects with a regression
test, and auto-ships low-risk fixes to `origin/develop`. Everything else is parked on a
`bugfix/<id8>` branch for review. Design: `docs/superpowers/specs/2026-10-07-bug-loop-design.md`.

## Stages

| Stage | Runs as | Can |
|---|---|---|
| Triage | `claude -p --tools ""` | classify only; reads the raw report |
| Fix | `claude -p` in the loop worktree | edit, build, run unit tests, commit on `bugfix/<id8>`; never push, never run E2E |
| Review | `claude -p --tools Read,Grep,Glob` | read the branch; never sees the report |
| Guard | `bugloop/guard.py` | protected paths, suspicious patterns, data field rules |
| Proof | orchestrator | test fails on base, passes on the fix (E2E scenarios run here, under the gate lock) |
| Gate | `verify.ps1 -Tier full` | build, unit, tool tests, E2E |

Every Claude stage runs with `--strict-mcp-config` (no MCP servers) and a scrubbed environment:
no stage gets `MMO_BUG_API_KEY` or `MMO_BUG_API_URL`; only the fixer gets
`MMO_E2E_MYSQL_PASSWORD`. The fixer also runs with `--disallowedTools "Bash(git push:*)"
"Bash(git -C * push:*)"` and git settings that make a push fail even if attempted:
`remote.origin.pushurl` and `remote.upstream.pushurl` set to
`blocked://bug-loop-fixer-cannot-push`, an empty `credential.helper`, a failing
`GIT_SSH_COMMAND` and `GIT_TERMINAL_PROMPT=0` (all through `GIT_CONFIG_*` variables of the
fixer process only; the orchestrator's own git calls are unaffected). Every build, test, gate and
Claude step that times out is killed with its whole process tree.

Proof, gate and ship all judge one commit, the fixer's `HEAD`: the branch must point at it
(otherwise needs-info, "fixer moved the branch"), the proof and the gate check it out detached,
and the ship refuses a branch that moved after gating and merges exactly that SHA. If the
worktree build cannot be configured, the bug is released as needs-info ("build configure
failed") before the fixer runs. Bug ids that are not 24 lowercase hex digits are skipped and
logged, since they become paths and branch names.

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

Regression tests have their own rules. `src/tests/` may hold C/C++ files and `e2e/scenarios/`
`.lua` files; anything else there parks. Test hunks skip the economy and condition rules but keep
the ones that could pull in or hide code: control characters, preprocessor directives (only an
`#include` of a header by a plain path is allowed), Lua `require`/`dofile`/`load`, and trailing
line continuations. At most 600 test lines per diff. A diff that changes only tests parks ("no
production change"). `tools/tests/` changes always park: they run on CI and in every gate run,
so they are never auto-shipped.

A diff larger than the reviewer's input limit (60 000 characters) parks ("diff too large for
review") before the review, proof and gate run, because the reviewer would only see part of it.

The regression-proof waiver is decided by the diff, not the fixer's claim: a fix may skip the
regression test only if it has at least one non-test change and all of its non-test changes are
under `data/editor/data/`, `data/client/Locales/` or `data/client/Interface/`. Client UI Lua
under `data/client/Interface/` needs no regression proof because it cannot cheat the server;
the guard and the full gate still apply.

A fix auto-ships only with: a fixable category, fixer confidence `high`, a clean review, an
empty guard, a passing proof (code fixes), a green full gate, fewer than 5 ships today, no
breaker, and a time outside 21:30–23:59 UTC (fixes ready during the window ship after it).

## Running it

Prerequisites:

- The standalone `claude` CLI must be logged in for the user account the scheduled task runs
  as: run `claude` once and `/login` (the desktop app's login does not cover it).
- Run `tools/bugs/adversarial_check.py` once; it must print 8 `ok` lines. Do this before the
  shadow day.
- `MMO_BUG_API_KEY` and `MMO_E2E_MYSQL_PASSWORD` as user environment variables, and
  `MMO_GATE_PYTHON` (the registration warns without it and falls back to `python` on PATH).
- A configured main build (`<main checkout>/build`) with `protoc` for the game-data decoder.
- Git push access to origin (ssh) and HTTPS access to the data submodule remotes.

Where the loop keeps its files is per machine, never in the repository:

| Directory | Default | Override (user environment variable) |
|---|---|---|
| Worktree (both data submodules plus its own build, 15–30 GB) | `mmo-bugloop` next to the main checkout | `MMO_BUGLOOP_WORKTREE` |
| Runtime snapshot (a few MB) | `mmo-bugloop-runtime` next to the main checkout | `MMO_BUGLOOP_RUNTIME` (or `-Runtime` at registration) |

The worktree may live on another drive than the repository, e.g. a large HDD
(`setx MMO_BUGLOOP_WORKTREE D:\mmo-bugloop`); builds there are slower but the loop is unattended.
The registration prints both resolved paths.

```powershell
$env:MMO_BUG_API_KEY = "<reader key>"
& $env:MMO_GATE_PYTHON tools/bugs/bug_loop.py watch --dry-run --once   # one shadow poll
& $env:MMO_GATE_PYTHON tools/bugs/bug_loop.py watch --dry-run          # shadow mode
powershell -File tools/bugs/register_bug_loop_task.ps1                 # live, at logon
```

The scheduled task runs `%LOCALAPPDATA%\mmo-bugloop\run_bug_loop.ps1`, a copy of
`tools/bugs/run_bug_loop.ps1` made at registration; re-run `register_bug_loop_task.ps1` to
update it. The launcher takes a snapshot of `origin/develop` (`tools/bugs` and
`src/shared/proto_data`) into the runtime directory with `System32\tar.exe` and runs the loop
from there, so loop changes only take effect once they are pushed to develop. The loop exits
with 75 at each UTC day boundary and the launcher starts it again from a new snapshot; any
other exit code (and a failed fetch, archive or extract) ends the task until the next logon.
The launcher logs to `tools/gate/reports/bugloop-task.log` (UTF-8).

At startup, before the first poll, the loop compiles the game-data schemas of the snapshot with
`protoc` from the main checkout's build (`H:/mmo/build/_deps/protobuf-build/*/protoc.exe`).
Nothing the fixer writes can change how later diffs are decoded. If protoc or the schemas are
missing, the loop logs a warning and runs without a decoder: every `.data` change then parks.

Stale claims: if the loop is killed mid-fix, the next live poll releases the bug as `triaged`
with an "interrupted" note. An interrupted guided refix goes back to `pr_open`; an interrupted
feature implement run goes back to `triaged` with the category `design_request` and "decide
again" in the note, so "Implement as feature" can be chosen again.

## Where to look

- `tools/gate/reports/bugloop-YYYY-MM-DD.json`: the day's outcomes, ships, parked branches,
  abuse flags, model invocations (`bugloop-dry-*.json` in shadow mode).
- `artifacts/bug-loop/<bug id>/`: `report.json`, `triage.json`, `FIX.json`, `diff.patch`,
  `guard.json`, `review.json`, `proof.json`, `gate.json`, `decision.json`. The accepted
  description of a feature is not stored here (the fixer can write this folder) but in
  `artifacts/bug-loop/state.json` under `features`.
- `artifacts/bug-loop/_runs/<date>.log`: the runner log. `dry-run-journal.jsonl`: the API
  writes a shadow run would have made.
- The bug's history in the web UI (`actor: bug-loop`). Abuse flags: web UI → Flagged reporters.

## Reviewing a parked fix

Parked fixes are expected and normal: measured on 761 small historical server edits, the guard
parks about 49%. They wait for review. The note on the bug names every reason it parked. Open
the bug in the web UI first: the decision panel shows the candidate diff and takes the
decision (see "Decisions and notifications"). The manual alternative still works: check out
`bugfix/<id8>`, review it, and `/ship` it. The loop notices the merge and marks the bug
resolved. To reject it by hand, set the bug to `wontfix` and delete the branch.

## Decisions and notifications

A parked bug has one of two statuses. `pr_open` is an ordinary park: the fix waits for review.
`needs_decision` means the reviewer asked a design question the loop cannot answer itself; it
also sends a Discord ping with the question. Every park uploads the candidate diff, so the web
UI can show it next to the decision panel.

The decision panel of a parked bug offers three actions: refix, ship and discard. A separate
"Feature request" panel offers a fourth, "Implement as feature", on rejected reports that are
not parked (see below). The loop consumes decisions before it does anything else in a poll:

- **Refix with guidance**: the fixer continues on the existing `bugfix/<id8>` branch with the
  maintainer's guidance, at most 3 guided rounds per bug. A refix with empty guidance is
  refused ("decide again"). If a guided refix fails, the bug goes back to `pr_open` so it can
  be decided again; at the limit the loop posts a "refix limit reached" message.
- **Ship**: ships exactly the recorded commit. The daily cap does not apply. If the branch tip
  no longer matches the recorded commit (or none is recorded, or the branch is gone), the bug
  is re-parked and the maintainer must decide again. The panel sends the SHA-256 of the diff it
  shows with the decision; the loop ships only if that matches the diff it uploaded for the
  recorded commit, otherwise it re-parks ("the diff you approved does not match the candidate
  commit; decide again") and uploads the real diff. The API refuses a new review diff while a
  decision is pending, the UI proxy refuses review-diff uploads altogether, and "Ship as is" is
  disabled until a diff is uploaded. An active circuit breaker also re-parks
  the bug ("the circuit breaker is tripped"); decide again once it is cleared. Only the
  nightly freeze window queues the ship until the window ends; a second ship decision for a
  queued bug only adds a note.
- **Discard**: sets the bug to `wontfix` and deletes the branch.
- **Implement as feature** (Feature request panel, only for `wontfix`/`not_a_bug` and
  `triaged`/`design_request` without a pending decision): the maintainer's description (at
  most 4000 characters) becomes the trusted expected behaviour for the fixer and the reviewer.
  Once the loop has claimed the bug it relabels the category to `feature` and keeps the
  description in its state; if the claim fails, the bug keeps its status and category and only
  gets a "decide again" note. A fresh `bugfix/<id8>` branch starts from `origin/develop`;
  guard, regression proof and gate run unchanged. A feature never ships on its own: every round
  parks with "feature: shipping needs the maintainer's approval", and the first green park of
  an implement run sends a "Feature ready for review" ping. It needs 2 invocations of budget
  and runs only live. A failed or interrupted implement run returns the bug to
  `triaged`/`design_request` with "decide again", so it can be implemented again. Once parked,
  the feature moves on through the usual refix, ship (hash-bound, as above) or discard; guided
  refixes keep the original description.

A discard or refix also drops a ship of the same bug that is still queued for the end of the
freeze window. Decisions without an action or already consumed are skipped, so an API from
before this feature (which ignores `decisionPending` and lists every bug) cannot trigger one.

In a dry run decisions are ignored and no Discord messages are sent.

Decisions need the maintainer key. Only the UI proxy holds it: `BUG_MAINTAINER_KEY` in the UI
container, accepted by the API through `BUG_MAINTAINER_KEYS`. The loop itself keeps using the
reader key; the browser never sees either key.

Rollout order: deploy the bug API with `BUG_MAINTAINER_KEYS` and the web UI with
`BUG_MAINTAINER_KEY` (the same key) *before* pushing develop. The loop's runtime snapshot updates
itself from `origin/develop` at the next day-boundary restart, and an old API breaks the new
loop. Then set `MMO_BUGLOOP_WEBHOOK` (and optionally `MMO_BUGLOOP_UI_URL`) as user environment
variables, re-register the task with `tools/bugs/register_bug_loop_task.ps1` (the task runs a
copied launcher, so a launcher change needs the re-registration) and restart it.

Discord is optional. Set `MMO_BUGLOOP_WEBHOOK` (the webhook URL) and optionally
`MMO_BUGLOOP_UI_URL` (the web UI base URL, used for links to bugs) as user environment
variables on the loop machine, then re-register the scheduled task
(`register_bug_loop_task.ps1`) and restart it so the launcher and the loop pick them up; the
loop logs `notifications: on` or `off` at startup (`off (dry run)` in a dry run). Without a
webhook nothing is sent. Messages:

- design decision needed (the reviewer's question, with a link and the branch);
- feature ready for review ("Feature ready for review": an implement run parked with an
  otherwise green result, with a link and the branch);
- circuit breaker tripped (a red nightly that lists a `Merge bugfix/...`);
- shipped (marked as a maintainer decision when it came from the panel);
- refix limit reached;
- every other status change of a bug (`notify_status_changes` in `bug_loop.json`, on by
  default): queued for a fix (with severity), fix / guided refix / feature implementation
  started, parked for review (first reasons and branch), ships after the freeze window,
  needs-info, duplicate, won't fix, abuse flag (never the reporter's account), design request,
  discarded, merged by hand, interrupted, loop error. Outcomes with a message of their own
  (shipped, refix limit, design decision needed, feature ready) are not repeated. Each message
  links the bug and quotes the triage summary;
- a daily summary (outcomes, Claude invocations against the budget, bugs waiting for a
  decision), sent once per UTC day when the day rolls over; it is persisted, so the restart at
  the day boundary does not send it twice or lose it;
- the launcher's stop notice, "Bug loop stopped with exit code N", sent when the launcher
  itself ends abnormally (any exit code other than 0, the day-boundary restart 75 or 3 for a
  second instance, and a failed fetch, archive or extract). Never in a dry run (`-DryRun`). It
  reads the variable directly and never logs the URL.

## CI watch and emergency fixes

The loop also watches GitHub Actions for `develop`. Without a token (and in a dry run) it logs
`ci watch: off` at startup and behaves as before; with one it logs `ci watch: on`. A red phase
still stored in the state from an earlier run with the watch on is then ignored, so nothing is
held, and the startup log warns: `ci watch: off, but state holds a red phase for ticket <id>;
ships are not held`.

- **Token:** user environment variable `MMO_BUGLOOP_GITHUB_TOKEN`, a fine-grained token for the
  repository with Actions read and write (write only to restart the Nightly Release) and
  Contents read.
- **Watched workflows:** `ccpp.yml` (Linux Servers, on every push to `develop` and to
  `bugfix/**`) and `nightly-release.yml`. Poll interval, workflow names and waits are the
  `ci_*` keys of the loop config (`ci_poll_seconds` 300, `ci_wait_minutes` 60,
  `ci_nightly_wait_minutes` 240, `emergency_attempts` 3).
- **Ticket:** when the newest run on `develop` is red, the loop opens an emergency ticket in the
  bug API (one per red phase) and sends a Discord message ("develop is red"). If GitHub cannot
  list the failing job yet, the ticket opens with "(log unavailable)" and the log is read again
  on a later poll. The ticket is resolved when develop is green again ("develop is green
  again"), even if the emergency fix is parked; a parked branch stays for the maintainer.
- **While develop is red** every other ship is queued with the reason "develop is red"
  (parking continues). A red result from the last read holds ships even before the phase could
  be opened. When GitHub is unreachable, the last known state holds: ships continue if develop
  was green and stay held if it was red. If the check fails for another reason (the bug API
  during the check, say), ships are queued with "CI check failed" until the CI state is
  confirmed again; triage and decisions go on meanwhile.
- **Discord:** the emergency ticket posts no per-attempt status messages; the CI messages
  ("develop is red", "Emergency fix shipped", "Emergency fix needs you", "develop is green
  again") are sent once per event.
- **Emergency fix:** the fixer works the ticket like a bug. Because Linux CI cannot run locally,
  the candidate is pushed to origin as `bugfix/<id8>` and the loop waits for the `ccpp.yml` run
  of that branch; only a green run ships it (Discord: "Emergency fix shipped"). The branch is
  deleted afterwards. Emergency fixes that touch `data/client` or `data/editor` are never
  pushed; they park. If the loop crashes right after the branch push, `bugfix/<id8>` stays on
  origin until the next emergency push of the same ticket; if the phase closes first, delete it
  by hand: `git push origin --delete bugfix/<id8>`. The emergency ship ignores the nightly
  freeze window: repairing develop before the nightly is the point.
- **After the ship** the loop waits for develop's own CI: 60 minutes, or 240 minutes when the
  Nightly Release decides (its gate alone can take about 180). A red run on the merge commit or
  on a descendant counts as a failed attempt. After 3 attempts the phase is parked and one
  Discord message "Emergency fix needs you" is sent. When an attempt is due but the daily
  invocation budget is used up, the same message says so once per phase and UTC day; develop
  stays red until the next UTC day or until the maintainer acts.
- **Maintainer decisions** on the emergency ticket steer the automation: discard parks the
  phase; ship merges the approved diff (hash-bound only, without a Linux CI run) and then waits
  for develop; refix gives the fixer guidance as for any bug, but a refix of the emergency
  ticket is never pushed to CI: it parks again for a ship decision.
- **Nightly Release:** if the Nightly Release is red, the loop starts a new Nightly Release run
  on develop (workflow_dispatch) once Linux Servers is green on a newer develop commit, at most
  once per commit and never while a nightly is queued or running. A restarted run that fails
  again counts as a failed attempt. A green release is not a deploy: `mmo-deployer` still
  ships it at 04:45.
- **Local nightly gate:** `nightly_gate.ps1` now gates `origin/develop` (after
  `git fetch origin`), the same commits the loop and CI look at.

Rollout order: deploy the bug API and UI first, then push `develop` (the `bugfix/**` workflow
trigger goes with it), then restart the loop task (`Stop-ScheduledTask`, kill any leftover
python process tree, start the task). Set `MMO_BUGLOOP_GITHUB_TOKEN` before the restart.

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

- Player-derived text still reaches the reviewer, by design: through the triage `observed`
  restatement and through the fixer's diff and claims. The reviewer never sees the raw report,
  and the guard, proof and gate do not read any of it.
- The fixer's isolation is defence in depth, not a sandbox: it runs with full shell access as
  the operator's user, so it could undo its own environment, or read a user-level secret or ssh
  key directly. What stops a manipulated fix from reaching develop is that only the orchestrator
  ships, and only a commit that passed the guard, proof and gate.

- Logic inversions that use neutral tokens.
- Edits to helpers or stat formulas far from any sensitive token.
- Lua names built at runtime.
- Reward changes that use neutral names and no literals.

The review stage and the gate are the remaining layers for these.
