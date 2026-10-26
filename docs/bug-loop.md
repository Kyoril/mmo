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
| Gate | `verify.ps1 -Tier fast` | build, unit, tool tests, Linux check (WSL); E2E runs in the nightly before deploy |
| Merge | `claude -p` in the loop worktree, then a read-only review | resolve conflicts with develop at ship and refix time; never stage, commit or push (see "Merge conflicts with develop") |

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
- `src/shared/{bot_core,bot_ai,bot_chat}/` (C/C++; bots can do nothing a player client cannot)
- `docs/` and `e2e/README.md` (`.md` only; prose nothing builds or loads)
- `data/client/ClientDB/<name>.data`, but only together with `data/editor/data/<name>.data`: the
  client copy is exported from the editor file, and the guard decodes and judges that one
- `src/tests/CMakeLists.txt` and `src/tests/<suite>/CMakeLists.txt`, line by line: only the shapes
  that register a suite (`mmo_add_test`, `add_subdirectory`, `target_sources`/`..._include_directories`/
  `..._compile_definitions(<target> PRIVATE` with source, directory and `MMO_*="${CMAKE_SOURCE_DIR}/..."`
  lines, `if (MMO_...)`, `if (EXISTS "${CMAKE_SOURCE_DIR}/...")`, `else()`, `endif()`, comments). Any other
  CMake line parks; every other `CMakeLists.txt` stays protected

The guard also parks: edits within 20 lines of a security-sensitive identifier or a rejection
path; changed server-code lines containing economy words (xp, reward, loot, drop, credit,
price, cost, ...) even without numbers; numeric literals in conditions or tuned values in server
code (client code cannot cheat the server, and comment lines never count); added or removed
preprocessor directives other than an `#include` of a header by a plain path and `#pragma once`
in a header; Lua `require`, `dofile` and
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
the guard and the gate still apply.

The fast gate runs last and only when it can change the outcome: a candidate that already parks
for another reason (guard, review, confidence, proof) skips it, and its `gate.json` says
"skipped". That saves the 15–25 minutes a gate takes, and nothing is lost: a maintainer's ship
decision merges the branch into today's develop and runs the fast gate on that merge before
anything is pushed. Emergency fixes and features always run the gate, because the CI push and the
"Feature ready" ping rely on a green result.

A fix auto-ships only with: a fixable category, fixer confidence `high` (or `medium` with a
passing regression proof, or `medium` on a data or UI fix that needs no proof and has a clean
review), a clean review, an empty guard, a passing proof (code fixes), a green
fast gate, fewer than `autoship_cap_per_day` (20) ships today, no
breaker, and a time outside 21:30–23:59 UTC (fixes ready during the window ship after it).

The reviewer splits what it finds: `blocking_issues` (the diff is wrong, unsafe or breaks a build
or test) park the fix, `advisory_notes` (follow-ups, other entries with the same gap, nice-to-have
audits, style) are kept in `review.json` and never park it.

### Self-correction

A candidate that would park only for reasons the fixer can repair itself does not go to the
maintainer yet. The loop runs another fix round on the same branch, up to `self_correct_rounds`
(2) per bug, each needing 2 invocations of budget. Repairable are: the reviewer's blocking issues
and its "does not fix the symptom", "not supported by the source", "out of scope" and "guidance not
followed" answers; a failed or missing regression proof; a red fast gate; fixer confidence
`medium`. Never repairable, so parked at once: any guard reason, a design question, a weakened
security property, an unusable review, a diff too large to review, emergency tickets (they have
their own attempts). A feature self-corrects the same way and still parks for approval at the end.

The fixer sees the previous round's reasons in its PREVIOUS ATTEMPT block, as untrusted findings,
never as maintainer guidance; a guided refix keeps its guidance in the rounds after it. Before a
round starts, the bug is parked on paper (`pr_open`, the branch, the uploaded diff, the backup),
so an interrupted or failed round leaves the last candidate reviewable. A maintainer refix starts
the count again.

## Running it

Prerequisites:

- The standalone `claude` CLI must be logged in for the user account the scheduled task runs
  as: run `claude` once and `/login` (the desktop app's login does not cover it).
- Run `tools/bugs/adversarial_check.py` once; it must print 8 `ok` lines. Do this before the
  shadow day.
- `MMO_BUG_API_KEY` and `MMO_E2E_MYSQL_PASSWORD` as user environment variables, and
  `MMO_GATE_PYTHON` (the registration warns without it and falls back to `python` on PATH).
- A configured main build (`<main checkout>/build`) with `protoc` for the game-data decoder.
- Git push access to origin (ssh) and HTTPS push access to the data submodule repositories on
  GitHub (the URLs in `.gitmodules`; Git Credential Manager must hold a token).

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

Stale git locks: a loop killed during a git operation leaves an `index.lock` in its worktree or a
submodule's git directory, and every later git command there fails ("File exists"). At startup
the loop removes such locks if they are older than a minute (it is the only user of its
worktree, and only one loop runs) and logs each one.

Stale claims: if the loop is killed mid-fix, the next live poll releases the bug as `triaged`
with an "interrupted" note and the category `needs_info` (see "Needs-info bugs"). An interrupted guided refix goes back to `pr_open`; an interrupted
feature implement run goes back to `triaged` with the category `design_request` and "decide
again" in the note, so "Implement as feature" can be chosen again.

### Moving the loop to another machine

The loop's local files (`artifacts/bug-loop/`, the worktree, its branches) do not have to move
with it. Every park copies the branch to origin as `refs/bugloop/bugfix/<id8>`, and the data
commits it points at to the same ref in the data repositories on GitHub (only those not already
on their `master`). These refs are not branches: no workflow runs on them and they do not show
up in the branch list. They are deleted when the fix ships, is merged by hand or discarded.

On the new machine a decision on a bug parked elsewhere still works:

- `report.json` is fetched from the API again, and `triage.json` is rebuilt from the triage
  the API keeps (no new model call, so the category cannot change under the maintainer);
- a refix restores the branch from its backup. Without a backup (parked before backups existed,
  or never parked, e.g. a needs-info bug sent back with a refix), the guided refix starts a
  fresh `bugfix/<id8>` from develop with the guidance;
- a ship restores the branch, takes its tip as the candidate and rebuilds the review diff from
  it, so the approved hash must still match what ships;
- a feature's description, kept in `state.json`, is recovered from the bug history (the newest
  `decision: implement: ...` entry with the actor `maintainer`).

Still per machine: the daily counters, queued ships, the refix count and the CI phase. Stop the
old task (`Disable-ScheduledTask -TaskName "MMO Bug Loop"`) before starting the new one; two live
loops at once double the daily ship cap and both work the same red develop. To review a branch
parked on another machine by hand: `git fetch origin refs/bugloop/bugfix/<id8>:bugfix/<id8>`.

## What is it doing right now?

```powershell
python tools/bugs/bug_loop.py status          # from the main checkout; --json for the raw file
```

The loop rewrites `artifacts/bug-loop/status.json` at every stage change (`status-dry.json` in a
dry run): its pid, the stage and since when (polling, triaging, preparing the worktree, fixing,
reviewing, waiting for the gate lock, proving, gating, shipping, sleeping until the next poll),
the bug and branch it works on, the fix queue (next bug first), today's invocations and
auto-ships against their limits, breaker and red develop. The command prints it and exits 1 when
the recorded process no longer runs ("NOT RUNNING"), so a loop that died is not mistaken for
one that waits. Run it from a worktree with `--repo <main checkout>`.

### Busy, idle and paused

The loop never sleeps between bugs: a poll that did work starts the next one at once. It sleeps
(`ci_poll_seconds`, 5 min, with the CI watch on, else `poll_seconds`) only after a poll that
found nothing to do. The mode in `state.json` (`mode`) names which of three it is in:

| Mode | When |
|---|---|
| busy | bugs are queued for a fix and the daily budget allows one |
| paused | bugs are queued but the daily invocation budget is used up (it resumes at 00:00 UTC) |
| idle | the fix queue is empty; the loop polls for new reports |

Each change is logged (`mode: idle -> busy`) and posted to Discord once ("Bug loop busy — N
bug(s) queued", "Bug loop paused — <reason>", "Bug loop idle", the last with what the busy
stretch shipped, parked and gave up on). The mode survives a restart, so the day-boundary restart
does not announce it again; the first mode after an upgrade is recorded silently. A dry run and
`notify_status_changes: false` post nothing. A worktree that cannot reach develop keeps its own
"stalled"/"resumed" messages and does not change the mode.

## Where to look

- `python tools/bugs/bug_loop.py status`: what the loop does right now (see above).
- `tools/gate/reports/bugloop-YYYY-MM-DD.json`: the day's outcomes, ships, parked branches,
  abuse flags, model invocations (`bugloop-dry-*.json` in shadow mode).
- `artifacts/bug-loop/<bug id>/`: `report.json`, `triage.json`, `FIX.json`, `diff.patch`,
  `guard.json`, `review.json`, `proof.json`, `gate.json`, `decision.json`, `merge.json` (merge
  conflicts with develop the loop resolved). The accepted
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
`bugfix/<id8>`, review it, and `/ship` it (a branch parked on another machine:
`git fetch origin refs/bugloop/bugfix/<id8>:bugfix/<id8>` first). The loop notices the merge and
marks the bug resolved. To reject it by hand, set the bug to `wontfix` and delete the branch.

## Needs-info bugs

The loop only polls `new`, so a bug it gives up on must land where the maintainer sees it. A
first fix attempt that ends without a commit to judge (the fixer answers `not_reproducible` or
`no_root_cause`, the fix stage fails, the build cannot be configured, the fixer leaves no clean
commit or moves the branch), an interrupted first attempt, and a report whose triage failed
twice are released as `triaged` with the triage category `needs_info`. Their note gives the
reason and ends with "waiting for the maintainer". The web UI lists them under the "Needs info"
filter (`status=triaged&triageCategory=needs_info`) and marks them with a chip; the daily summary
counts them under "waiting for a decision" and lists the day's needs-info outcomes with their
reason.

To act on one, read the note and the artifacts (`FIX.json` holds the fixer's reasoning), then
either set the status back to `new` (the loop forgets its earlier attempt, triages the report
again and, for a defect, tries a fresh fix) or close it (`wontfix`, `resolved`, `duplicate`).
Failed guided refixes, feature runs and emergency attempts keep their own paths: back to
`pr_open`, back to `design_request`, and retried by the CI watch.

The fixer sees both builds of a report in a REPORT BUILDS block: `serverBuild` (the world
server's git commit, from the server) and `clientBuild` (from the player's client, untrusted).
Server-side behaviour is judged against `serverBuild`: a supposed fix that is already an
ancestor of it did not fix the bug. (On 2026-10-11 the fixer called a Hollow Choir boss skip
"already fixed on develop" by comparing against the client build; the server already had the
supposed fix.)

## Decisions and notifications

A parked bug has one of two statuses. `pr_open` is an ordinary park: the fix waits for review.
`needs_decision` means the reviewer asked a design question the loop cannot answer itself; it
also sends a Discord ping with the question. Every park uploads the candidate diff, so the web
UI can show it next to the decision panel.

The decision panel of a parked bug offers three actions: refix, ship and discard. A separate
"Feature request" panel offers a fourth, "Implement as feature", on rejected reports that are
not parked (see below). The loop consumes decisions before it does anything else in a poll:

- **Guidance sets game values.** A player report can never make rewards, drops, prices, stats,
  costs or cooldowns more generous; maintainer guidance and feature descriptions can, and the
  fixer makes exactly the requested change (the reviewer does not count it as a security
  reduction). Such changes still park for a ship decision, since the guard parks every numeric
  economy change. Weakening a permission, validation or security check stays refused, whoever
  asks. (On 2026-10-26 the fixer refused a guided price raise for item 87.)
- **Refix with guidance**: the fixer continues on the existing `bugfix/<id8>` branch with the
  maintainer's guidance, at most 3 guided rounds per bug. Before the fixer starts, the loop
  merges today's `origin/develop` into the branch itself; a data submodule pointer that both
  moved is resolved by merging the two submodule commits. The refix is then guarded and
  reviewed against today's develop, so data that landed meanwhile (another fix's mesh, say) is
  not counted as part of this fix. Conflicts there are resolved as described in "Merge conflicts
  with develop"; one nothing can resolve restarts the refix on a fresh `bugfix/<id8>` from
  develop, and the fixer re-applies the earlier change (its diff is in the fixer's input)
  together with the guidance. Shipping
  merges submodule pointers the same way, and also merges a submodule `master` that moved past
  develop's pointer (data pushed without a develop bump) into the fix's submodule commit; that
  merge result always runs the fast gate before anything is pushed. A refix never has to merge
  submodule master itself: it would lose the
  same race again before it ships. A refix with empty guidance is
  refused ("decide again"). If a guided refix fails, the bug goes back to `pr_open` so it can
  be decided again; at the limit the loop posts a "refix limit reached" message.
- **Ship**: ships exactly the recorded commit. The daily cap does not apply. If the branch tip
  no longer matches the recorded commit (or none is recorded, or the branch is gone), the bug
  is re-parked and the maintainer must decide again. The panel sends the SHA-256 of the diff it
  shows with the decision; the loop ships only if that matches the diff it uploaded for the
  recorded commit, otherwise it re-parks ("the diff you approved does not match the candidate
  commit; decide again") and uploads the real diff. The API refuses a new review diff while a
  decision is pending, the UI proxy refuses review-diff uploads altogether, and "Ship as is" is
  disabled until a diff is uploaded. An active circuit breaker queues the ship
  ("the circuit breaker is tripped; ships once the breaker is cleared"), and so do the nightly
  freeze window and a red develop: the decision is never used up by a hold, and the ship goes
  out on its own once the hold ends. A second ship decision for a queued bug only adds a note.

  Every ship (auto or decided) lands in the same order: each changed data submodule commit is
  pushed, fast-forward only, to `master` of the submodule's repository on GitHub (the remote
  `upstream` in the loop worktree's submodule, set on every prepare from the URL in the
  committed `.gitmodules`), then `develop` is pushed to origin. Develop therefore never points at
  a data commit GitHub does not have, and a rejected submodule push stops the ship before
  develop moves. A push rejected only because develop (or a submodule `master`) moved while the
  merge was gated starts the ship over: fetch, merge onto the new tip, gate again, push; at most
  3 rounds ("develop kept moving: 3 attempts"). On 2026-10-24 a ship parked after losing that race
  once. The URL never comes from the main checkout's `.git/config`, which may point a
  submodule at the main checkout's own `data/client` (a non-bare repository with `master`
  checked out: pushes there are refused, and nothing downstream fetches from it). The main
  checkout's submodules are not updated; see Known limits. Because they can lag behind, a
  `git submodule update` in the loop worktree that fails (a fresh worktree cloned its
  submodules from the main checkout: "not our ref") fetches the `.gitmodules` remote and retries
  once.
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
- the loop's mode changes: busy, paused (budget used up with bugs queued), idle (see "Busy, idle
  and paused"), sent with the status messages and switched off with them;
- a daily summary (outcomes, Claude invocations against the budget, bugs waiting for a
  decision: parked ones without a decision and `needs_info` ones, plus the day's parked and
  needs-info outcomes with their first reason), sent once per UTC day when the day rolls over; it is persisted, so the restart at
  the day boundary does not send it twice or lose it;
- the launcher's stop notice, "Bug loop stopped with exit code N", sent when the launcher
  itself ends abnormally (any exit code other than 0, the day-boundary restart 75 or 3 for a
  second instance, and a failed fetch, archive or extract). Never in a dry run (`-DryRun`). It
  reads the variable directly and never logs the URL.

## Merge conflicts with develop

A fix waits for review for hours or days while develop moves on, so its merge often conflicts.
Before 2026-10-22 every conflict parked the fix with "needs a manual merge", and neither a ship
decision nor a refix could get past it. Now the loop resolves them itself, at every merge it
makes (ship, refix, and a submodule `master` that moved), in this order:

1. **Game data** (`*.data` in `data/editor` and `data/client`) is merged by entry
   (`bugloop/data_merge.py`), without a model: entries are matched by id, fields by number, and
   lists without ids as sequences, where edits apply unless they overlap (spawns appended next to
   re-snapped heights both apply). With the schema the loop compiles at startup, two edits to
   different words of one string merge (one side turns "20 to 22" into "$s0", the other "40%"
   into "$s1%"), and a spawn one side moved keeps its height over develop's height re-snap.
   What stays a conflict: the same value changed differently on both sides, and an id both sides
   added for different entries ("an id collision"). Those go to the re-apply (see below), where
   the fixer redoes the change on today's develop and picks a free id. Replayed against the 62
   historical merges in both data repositories where the two sides changed one `.data` file, 54
   come out as the humans merged them; the other 8 are 7 id collisions and one sentence written
   differently on both sides. git never merges these files itself during the loop's merges
   (`*.data -merge`), because a line merge of protobuf bytes that happen to contain no NUL byte
   would corrupt them silently.
2. **Text files** go to a Claude merge stage (`prompts/merge.md`, all tools but push, in the loop
   worktree, `merge_max_usd` per run). It edits the conflicted files only; the loop then checks
   without trusting it: HEAD unchanged, no conflict markers, no other file staged, changed or
   created. A second, read-only stage (`prompts/merge_review.md`) must confirm that the resolution
   keeps both sides and adds nothing else. It sees only the conflicts (base, OURS, THEIRS, the
   resolution and a few lines around each), once the loop has checked that everything git merged
   cleanly came back unchanged; a resolution that also changed text outside the conflicts is
   shown as full diffs against both sides. On 2026-10-23 the full diffs of a much-edited
   `GlueTemplates.xml` were 112 000 characters and the review refused them; its one conflict is
   9 000.
   Protocol bumps are the one exception to "conflicted files only": when a protocol file
   (`game_protocol.h`, `auth_protocol.h`, `protocol_fingerprint.json`, `protocol_versions.md`)
   conflicts, the resolver may also change the others. Both sides bumping to the same value merges
   the header cleanly, yet the merge needs a version of its own (b36cdaa3, 2026-10-23). A version
   header may change only in its `ProtocolVersion` line, and only upwards; the review sees these
   changes as diffs, and the gate's protocol check verifies the fingerprint. Both protocol version bumps colliding are resolved to one above both, with the
   fingerprint regenerated.
3. Other binary files (meshes, tiles) changed on both sides stay conflicts.

A resolved merge always runs the fast gate before anything is pushed (it is a merge with a moved
develop), and the merge commit names the resolved files ("Conflicts with origin/develop resolved
by the bug loop in: ..."). The reports are in `artifacts/bug-loop/<bug id>/merge.json`, and the
ship note on the bug lists the files. The resolution needs two Claude invocations; without budget
left the conflict stands.

**Re-apply.** When a ship (auto or decided) still hits a conflict nothing could resolve, the
bug is parked as before and the loop at once re-applies the fix on today's develop: a guided
refix whose guidance comes from the loop ("From the bug loop, not the maintainer: ... re-apply
the same fix on today's develop"), at most `max_reapply` (2) times per bug and only with budget
for it. It does not use up the maintainer's refix rounds. The new candidate goes through guard,
review, proof and gate again; it auto-ships only if it qualifies on its own, otherwise it parks
and needs a ship decision again (the approved diff no longer matches).

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
  `ci_nightly_wait_minutes` 240, `emergency_attempts` 3, `emergency_retry_minutes` 30). An
  emergency attempt that ends in an internal loop error (other than an unreachable worktree,
  below) counts, and the next one waits
  `emergency_retry_minutes` (the ticket note says when), so a persistent fault cannot use up
  every attempt within seconds.
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
  freeze window: repairing develop before the nightly is the point. A tripped circuit breaker
  parks a green emergency fix ("Emergency fix needs you"), with one exception: the loop tripped
  the breaker itself for a red Nightly Release run of the current red phase. Then the fix
  repairs what tripped it and ships (ticket note "shipping despite the breaker"); the breaker
  stays tripped.
- **After the ship** the loop waits for develop's own CI: 60 minutes, or 240 minutes when the
  Nightly Release decides (its gate alone can take about 180). A red run on the merge commit or
  on a descendant counts as a failed attempt. After 3 attempts the phase is parked and one
  Discord message "Emergency fix needs you" is sent. When an attempt is due but the daily
  invocation budget is used up, the same message says so once per phase and UTC day; develop
  stays red until the next UTC day or until the maintainer acts.
- **Worktree unavailable:** when the loop cannot bring its worktree to origin/develop before a
  fix (fetch, checkout or `git submodule update` fails, e.g. develop was pushed before its
  `data/client` commit reached origin: "not our ref"), that is the loop's fault, not the bug's.
  Nothing is touched: the bug stays queued and unclaimed, the emergency ticket stays `triaged`,
  and no attempt (emergency or otherwise) is used. Every fix then waits `ci_poll_seconds`
  before the worktree is tried again; the emergency ticket still goes first once it works.
  After `worktree_escalate_minutes` (60) of failures the maintainer gets one Discord message
  "Bug loop stalled" with the git error, and "Bug loop resumed" when it recovers. The
  back-off is `worktree_down` in `state.json`. A maintainer decision (refix, implement) that
  hits the same failure is consumed as before and goes back to the maintainer.
- **Maintainer decisions** on the emergency ticket steer the automation: discard parks the
  phase; ship merges the approved diff (hash-bound only, without a Linux CI run) and then waits
  for develop; refix gives the fixer guidance as for any bug, but a refix of the emergency
  ticket is never pushed to CI: it parks again for a ship decision.
- **Nightly Release:** if the Nightly Release is red, the loop starts a new Nightly Release run
  on develop (workflow_dispatch) once Linux Servers is green on a newer develop commit, at most
  once per commit and never while a nightly is queued or running. A restarted run that fails
  again counts as a failed attempt. A green release is not a deploy: `mmo-deployer` still
  ships it at 04:45.

Rollout order: deploy the bug API and UI first, then push `develop` (the `bugfix/**` workflow
trigger goes with it), then restart the loop task (`Stop-ScheduledTask`, kill any leftover
python process tree, start the task). Set `MMO_BUGLOOP_GITHUB_TOKEN` before the restart.

## Circuit breaker

`artifacts/bug-loop/BREAKER` stops all auto-shipping; parking continues. It trips by itself
when the newest finished GitHub "Nightly Release" run on develop failed and a `Merge bugfix/...`
landed on develop since the last successful run (read through `gh` in the user's login, see
`bugloop/ci.py`). If `gh` cannot be reached, the loop logs it and keeps going without the check.
After a deploy rollback, trip it by hand. Only clear it once the cause is understood.

A breaker the loop tripped itself clears itself once a newer Nightly Release on develop is
green (Discord: "circuit breaker cleared"): the trip is a correlation (a red nightly with a
bug-loop merge in it), and a green nightly after it shows develop no longer has the fault.
On 2026-10-20 a red nightly caused by a bot change held seven ship decisions after the next
nightly was already green. A breaker set by hand, or `breaker on` on top of the loop's trip,
never clears itself. Ship decisions wait in the ship queue while the breaker is tripped and
ship when it clears; auto-ship candidates still park.

The one exemption: when the loop tripped the breaker for a red nightly run, it records that run
id (`breaker_source` in `state.json`, together with the breaker file's text). A green emergency
fix of a red phase that saw that same nightly run red ships despite the breaker. Everything
else stays blocked, and the breaker stays tripped after the emergency ship: reset it yourself
as always. A breaker set by hand (`breaker on`), tripped for another run, or reset and set
again, keeps blocking the emergency fix too; it parks with "Emergency fix needs you".
`breaker on` on an already tripped breaker appends a line marked "(set by hand)", so a
hold set on top of the loop's own trip also ends the exemption.


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
- A data change that develop changed differently in the same value (the same spawn moved on
  both sides, say) cannot be merged; the loop re-applies the fix on today's develop instead
  (see "Merge conflicts with develop").
- After an auto-ship, the local `develop` in `H:/mmo` is behind `origin/develop`, and so is the
  `master` of its data submodules. Pull before pushing your own work (`git pull` then
  `git -C data/client pull` / `git -C data/editor pull`, or `git submodule update`). The loop
  never writes into the main checkout's submodules.
- After changing `prompts/triage.md`, run `tools/bugs/adversarial_check.py` (real model calls).
- The bug API client retries a GET, PUT or PATCH once when the connection is reset or times
  out, and the first such failure switches it to TLS 1.2 for the rest of the process. POSTs
  (claims, emergency tickets) are never repeated: a claim that went through before the reset
  would come back as 409, "claimed elsewhere". On 2026-10-23 every TLS 1.3 handshake with the API
  was reset for hours, and the loop could not see any ship decision; later the same day a reset
  during a status update left a finished fix "interrupted".

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
