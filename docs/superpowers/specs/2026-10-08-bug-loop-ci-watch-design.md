# Bug Loop — CI Watch and Emergency Fixes — Design

Date: 2026-10-08
Status: approved in brainstorming, awaiting spec review
Builds on: `docs/bug-loop.md`, `docs/superpowers/specs/2026-10-07-bug-loop-decisions-design.md`

## Goal

When `develop` breaks in GitHub CI — as on 2026-10-07, when four maintainer-approved fixes, each
green on its own on Windows, together crashed `game_server_tests` on Linux with a double free — the
system notices it without a human, opens an emergency ticket and repairs `develop` on its own,
verified on the platform that broke. Until `develop` is green again nothing else ships.

## Decisions (from brainstorming)

- Detection: the bug loop polls GitHub Actions with a read-only token (option A).
- Verification: GitHub CI is the judge; the loop pushes the emergency branch and waits for the Linux
  run (option A).
- No automatic revert: if the emergency fix fails, `develop` stays red and the maintainer is told
  (option B).
- Emergency fixes ship under the normal rules (guard, review, proof), plus a green Linux CI run;
  outside the daily cap and through the red-develop stop (option A).

## Non-goals

- Automatic reverts of any commit.
- Watching branches other than `develop`, or workflows other than the configured ones.
- Treating human commits differently: a red caused by a human commit gets the same ticket and
  emergency fix as one caused by a loop merge; nothing is ever reverted.

## 1. Detection

- New config/env: `MMO_BUGLOOP_GITHUB_TOKEN` (fine-grained token, repository `Kyoril/mmo` only,
  permissions Actions: read and write (write only to start the Nightly Release workflow),
  Contents: read). Owner and repository come from the main repository's
  `origin` URL; no names or machine paths are hardcoded. Without a token the watch is off and the
  loop logs `ci watch: off (no token)` at startup.
- Watched workflows (config, defaults): `ccpp.yml` ("Linux Servers", every push to `develop`) and
  `nightly-release.yml` ("Nightly Release", its gate job).
- Poll at most every 5 minutes (each poll cycle checks whether the interval passed). Stdlib HTTP
  only. GitHub errors and rate limits are logged and never stop the loop.
- Per watched workflow, the newest completed run decides (for "Linux Servers" runs on branch
  `develop`; for "Nightly Release" runs started by the schedule or by `workflow_dispatch`, which
  always build `develop`): conclusion `failure` → that workflow is red; `success` → green;
  `cancelled`/`skipped` → no change. `develop` is red when any watched workflow is red; it is green
  again when every watched workflow's newest completed run succeeded.
- A red nightly does not wait for the next night: once a commit newer than the failed nightly's
  commit is on `develop` and "Linux Servers" is green for it (normally right after the emergency fix
  ships), the loop starts the real Nightly Release (`workflow_dispatch`, no `force`). It gates and
  publishes a release; publishing is not deploying — `mmo-deployer` stages it and deploys it in the
  next maintenance window, as with every nightly. At most one such start per red phase and `develop`
  commit; a start that is red again counts as a failed emergency attempt.
- On red: download the failing job's log (`/actions/jobs/{id}/logs`) and extract a bounded excerpt
  (≤ 8000 characters): failed test names with file:line, the assertion/abort lines, compiler
  `error:` lines, and the failing step name. Suspects: `git log` from the newest green run's commit to
  the red commit, loop merges marked (`Merge bugfix/<id8> (bug-loop, ...)`).
- Loop state gains `ci`: per workflow the last seen run id and colour, plus the current red phase
  (`since` time, red commit, emergency ticket id, attempts, last notified event).

## 2. Emergency ticket (bug API)

- New route `POST /api/bugs/system` (reader key, as the loop holds only that key). Body:
  `{ kind: 'ci_failure', summary, details, commit, runUrl }`; `summary` ≤ 300, `details` ≤ 8000.
  It creates a bug with `source: 'system'`, `status: 'triaged'`, `triage: { category: 'ci_failure',
  severity: 'emergency', summary }`, `subject: { type: 'generic' }`, `comment: summary`,
  `logTail: details`, a history entry `created by the bug loop: <runUrl>`.
- `Bug.source` is a new field, enum `player | system`, default `player`; the ingest route always
  stores `player`. Only one open (`status` not `resolved`/`wontfix`/`duplicate`) `ci_failure` ticket
  may exist: a second create answers `409` with the open ticket's id.
- PATCH gains an optional `logTail` update allowed only for `source: 'system'` bugs, so a later red
  run of the same phase can refresh the excerpt; the run URL goes into a note.
- Trust: the loop accepts a ticket as an emergency only if its id is the one recorded in its own
  state (`ci.ticket`), never because of API fields. The ticket text is the loop's own; the log excerpt
  is data (it can contain output of code that came from player reports) and is nonce-fenced like
  player text.

## 3. Emergency fix

- An open emergency ticket goes before every other bug. It skips triage (the loop wrote it).
- Fixer input: a trusted `CI FAILURE` instruction block ("develop is red in Linux CI; reproduce,
  fix the root cause, never weaken, skip or delete a test") plus the fenced log excerpt and the
  suspect list. New section in `prompts/fix.md`; the reviewer gets the same blocks except the raw
  excerpt is replaced by the extracted failing-test lines.
- Locally the normal pipeline runs: fixer, review, guard, regression proof, full gate. The failing
  CI test is the regression test; when the failure does not reproduce on Windows the local proof is
  waived for this ticket only (reason recorded), because the CI run below is the proof.
- CI verification: when everything local is green the loop pushes `bugfix/<id8>` to origin (the
  only branch push the loop may make, and only for its emergency ticket) and records a pending CI
  check (branch, head, deadline 60 min) in state. The branch is verified by "Linux Servers" only (the
  Nightly Release always builds `develop`); a failure only the nightly sees (for example a Linux-only
  E2E failure) is verified by the nightly start after the ship, see section 1. The poll cycle checks it without blocking other
  work. The workflow "Linux Servers" gets a `bugfix/**` push trigger.
- Result:
  - CI green and guard clean → ship to `develop` (fast gate on the merge as today), outside the daily
    cap and despite the red-develop stop; delete the origin branch.
  - CI green but parked (guard, review, design question) → park for the maintainer with the note
    "Linux CI green on <sha>" and an emergency Discord message.
  - CI red or deadline passed → refix with the new log, up to 3 attempts per red phase, then park
    with an emergency Discord message.
- When `develop` turns green (by any commit) the loop resolves the ticket with a note and clears the
  red phase. A parked emergency fix then stays parked for the maintainer as usual.

## 4. Red-develop stop and notifications

- While `develop` is red nothing ships automatically except the emergency fix: auto-ships and
  maintainer "ship as is" decisions go to the existing ship queue with the reason
  "develop is red" and ship one by one after it turns green (each with the fast gate on the merge).
  Refix and discard decisions run as usual. The circuit breaker blocks every ship as before, with
  one exception: a green emergency fix ships when the loop tripped the breaker itself for a red
  Nightly Release run that belongs to the current red phase. The breaker stays tripped.
- Discord (once per event, never per poll): "develop is red" (workflow, failing test, run link,
  ticket link), "emergency fix shipped", "emergency fix needs you" (parked or out of attempts),
  "develop is green again".

## 5. Side fixes

- `tools/gate/nightly_gate.ps1`: fetch and gate `origin/develop` instead of the local `develop` ref,
  so the local nightly sees the loop's pushes (it gated a stale `8715c97c` on 2026-10-08). Takes
  effect one night after it reaches develop.
- `.github/workflows/ccpp.yml`: push trigger for `bugfix/**`.
- Docs: `docs/bug-loop.md` (CI watch, emergency path, token), `CLAUDE.md` Agentic Workflow (the loop
  may also push `bugfix/*` branches, for emergency tickets only).

## Web UI

- An `Emergency` chip (error colour) for `triage.category = 'ci_failure'` in the list and on the
  page; the page shows `logTail` as a monospace block for system bugs.
- The category filter offers `ci_failure`.

## Security notes

- The token can read Actions and contents only; the loop's push rights are unchanged (its own SSH
  setup), and the new branch push is limited in code to `bugfix/<id8>` of the recorded emergency
  ticket.
- Guard, review and proof still apply to emergency fixes; only the local proof can be waived, and
  only in favour of the CI run.
- Log excerpts are untrusted data; the instruction block is fixed loop text.
- The breaker exemption needs the run id the loop itself recorded when it tripped the breaker,
  and the breaker file unchanged since; a manual trip can never be bypassed.

## Testing

- GitHub client with a fake transport: run selection per workflow, red/green transitions,
  cancelled/skipped runs, log excerpt extraction from a captured CI log, rate-limit/HTTP errors.
- Orchestrator: red creates one ticket and pauses ships (queued with the reason); emergency goes
  first; local green → branch push + pending check; CI green → ship outside the cap; CI red → retry,
  3 attempts → park + message; green develop resolves the ticket; no token → watch off; the branch
  push refuses anything but the emergency branch.
- API: system create (reader key, single open ticket, 409), source default, logTail PATCH only for
  system bugs.
- UI: chip and filter; build.

## Rollout

Deploy API and UI first, then push develop (the workflow trigger change goes with it), set
`MMO_BUGLOOP_GITHUB_TOKEN` (done), restart the loop task.
