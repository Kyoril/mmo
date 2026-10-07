# Bug Loop — Maintainer Decisions and Notifications — Design

Date: 2026-10-07
Status: approved in brainstorming, awaiting spec review
Builds on: `docs/superpowers/specs/2026-10-07-bug-loop-design.md`, `docs/bug-loop.md`

## Goal

A parked bug must never sit unnoticed. When the bug loop needs a human, the maintainer is told
right away (Discord), sees the question and the candidate fix in the web UI, and answers there
with one of three actions. The loop picks the answer up and continues on its own.

Success looks like the first live case: the bandit-assist fix was parked because the review found
a design question (assist chains without limit, no line-of-sight check). With this feature the
maintainer gets a Discord message with that question, writes "limit the chain to one level, check
line of sight" in the UI, and the loop builds that, verifies it and ships it — or parks again
with reasons.

## Non-goals

- Discussion threads, multiple maintainers, per-user permissions. One maintainer key.
- Showing the full artifact folder in the UI. Only the diff and the decision-relevant fields.
- Changing what auto-ships without a human. The guard, review and gate rules stay as they are.

## Parts and repositories

| Part | Repository | Change |
|---|---|---|
| Bug API | `H:\mmo-error-report` | `needs_decision` status, `designQuestion`, `reviewDiff`, `decision` fields, maintainer key + decision route, two filters |
| Web UI | `H:\mmo-error-report-ui` | decision block on the bug page, list filter, navigation badge, proxy route with the maintainer key |
| Bug loop | `H:\mmo` (`tools/bugs`) | `design_question` in the review, `needs_decision` parking, decision handling (refix / ship / discard), Discord notifier, daily summary |

## Bug API

### Data

- Status list gains `needs_decision` (between `pr_open` and `resolved` in the UI order).
- `designQuestion: String` (≤ 2000 chars, default `''`): the reviewer's question for the maintainer.
- `reviewDiff: String` (≤ 128 KB, default `''`): the candidate diff of the parked branch.
- `decision`: `{ action: 'refix' | 'ship' | 'discard', guidance: String (≤ 4000), decidedAt: Date, consumedAt: Date | null }`, default `null`.

### Keys

A third key list, `BUG_MAINTAINER_KEYS`, fail-closed like the other two (empty list = route
refuses everything). Only the decision route accepts it. The loop never holds a maintainer key;
only the web UI proxy on the server does.

### Routes

- `POST /api/bugs/:id/decision` — maintainer key only. Body `{ action, guidance }`.
  - `400` unless `action` is one of the three; `refix` requires non-empty `guidance`.
  - `409` unless the bug's status is `pr_open` or `needs_decision`, or while an unconsumed
    decision exists.
  - Sets `decision = { action, guidance, decidedAt: now, consumedAt: null }`, appends history
    `{ actor: 'maintainer', change: 'decision: <action>: <guidance>' }`.
- `PATCH /api/bugs/:id` (reader key, existing) additionally accepts:
  - `designQuestion` (string, truncated to 2000),
  - `decisionConsumed: true` → sets `decision.consumedAt = now` (only if a decision exists),
    history `decision consumed`.
  It never accepts `decision` itself.
- `PUT /api/bugs/:id/review-diff` — reader key, body `{ diff }` up to 128 KB (own JSON parser
  limit), stores `reviewDiff`. Separate route because `PATCH` keeps its 32 KB limit.
- `GET /api/bugs` gains two filters:
  - `decisionPending=true`: `decision` exists and `decision.consumedAt` is null.
  - `awaitingDecision=true`: status `pr_open` or `needs_decision`, and no pending decision.
  The list response keeps excluding heavy fields and now also excludes `reviewDiff`.

## Bug loop

### Review and parking

- `schemas/review.json` and `prompts/review.md` gain `design_question` (string; empty when the
  change needs no product or design decision) and `guidance_followed` (boolean; true when no
  maintainer guidance was given).
- `review_blockers`: a non-empty `design_question` is a blocker ("review: design question: …");
  `guidance_followed: false` is a blocker.
- `_park(bug, branch, reasons, review)`:
  - status `needs_decision` when the review's `design_question` is non-empty, else `pr_open`;
  - writes `designQuestion` (PATCH) and uploads the diff (`PUT review-diff`, truncated to 128 KB
    with a trailing marker);
  - sends a Discord "design question" message for `needs_decision`.
- `_reconcile_parked` also lists `needs_decision` bugs (a branch the maintainer `/ship`ped by
  hand still resolves the bug).

### Decision handling

At the start of every live poll, before stale-claim release and triage, the loop lists
`decisionPending=true` and handles each bug (ids validated as before). Every handled decision is
marked `decisionConsumed: true` first, so a failure cannot replay it. The fixer, triage and
review processes never see the decision route or the maintainer key.

- **refix**
  - Allowed at most 3 times per bug (counter in the loop state). The 4th refix decision parks
    with "refix limit reached; finish it by hand".
  - Claims the bug, prepares the worktree, checks out the existing `bugfix/<id8>` branch at its
    current tip (no new branch), and runs the fixer with extra input blocks:
    - `PREVIOUS ATTEMPT` (model output, verify): the previous `FIX.json` claims, the park reasons;
    - `MAINTAINER GUIDANCE` with provenance `maintainer, trusted`: the guidance text.
  - `prompts/fix.md`: the maintainer guidance block is trusted and binding (it outranks the
    player report and the previous attempt); every other rule stays, including never weakening
    checks. Guidance cannot widen what the fixer may touch.
  - The reviewer gets the guidance block too and answers `guidance_followed`.
  - From there the normal path runs: guard, review, proof, full gate, then ship or park.
- **ship**
  - Ships exactly the commit the maintainer saw: the `head` recorded in the bug's latest
    `decision.json`. If `bugfix/<id8>` no longer points at that commit, it parks with
    "branch moved since it was parked; decide again".
  - Uses the normal ship path (`gitops.ship`: submodules first, fast gate when develop moved,
    non-force push). Guard and review are replaced by the maintainer's decision; the daily
    auto-ship cap does not apply. The circuit breaker and the freeze window still apply (inside
    the window the ship is queued as today).
  - Outcome `shipped-by-maintainer`; status `resolved` with the commit in the note.
- **discard**
  - Status `wontfix`, note `discarded by maintainer: <guidance>`, local branch deleted, claim
    released. Outcome `discarded`.

### Discord notifier

- New module `bugloop/notify.py`: `Notifier(webhook_url, run=urlopen)` with `send(text)`.
  Discord JSON (`content`), `User-Agent: mmo-bug-loop/1` (Discord's Cloudflare front rejects
  urllib's default agent), messages cut to 2000 characters, timeout 10 s. Failures are logged
  and swallowed; the URL never appears in a message or log line.
- Configuration: user environment variable `MMO_BUGLOOP_WEBHOOK`; unset means no messages.
  Optional `MMO_BUGLOOP_UI_URL` (e.g. `https://…`) to link bugs as `<ui>/bugs/<id>`.
  Both are added to the environment scrubbed from every Claude stage.
- Messages contain only the triage summary (model restatement, cut to 200 characters), the
  review's design question, park reasons and ids — never the raw player comment or log tail.
- Immediate messages:
  - design question (status `needs_decision`): title line, the question, branch, link;
  - circuit breaker tripped, with its reason;
  - shipped (automatic or by maintainer): one line with bug and commit;
  - refix limit reached.
- Daily summary, sent once when the loop notices a new UTC day and before the counters reset:
  shipped, parked (one-line reason each), abuse flags, Claude invocations used, decisions still
  open (`awaitingDecision` count).
- Launcher (`run_bug_loop.ps1`): when the loop exits with a code other than 0 or 75, post
  "bug loop stopped with exit code N" to the webhook (same User-Agent), then exit as today.

## Web UI

- Proxy (`nginx/default.conf.template`): a regex location for
  `^/api/bugs/[0-9a-f]{24}/decision$` that proxies with `X-Api-Key: ${BUG_MAINTAINER_KEY}`.
  Regex locations win over the `/api/bugs` prefix location, so every other bug route keeps the
  reader key. New env var `BUG_MAINTAINER_KEY` in the UI container (documented in
  `nginx/README.md` and `docker-compose.yml`). The site stays behind basic auth.
- Bug page, for status `needs_decision` or `pr_open`:
  - a "Decision" panel: the design question (if any), the park reasons (latest bug-loop note),
    the branch, and the diff (`reviewDiff`, monospaced, collapsible);
  - a "Guidance" text field and three buttons: **Refix with guidance** (requires text),
    **Ship as is** and **Discard** (both ask for confirmation);
  - when `decision` is pending: the panel shows "Decision sent; the loop handles it on its
    next poll" and the buttons are disabled.
- Bug list: a filter "Waiting for decision" (`awaitingDecision=true`) and `needs_decision` in
  the status filter.
- Navigation: a badge with the `awaitingDecision` count next to "Bugs", refreshed on page load.

## Security notes

- Decisions are trusted input. They can only be created with the maintainer key, which exists
  only on the server in the UI proxy's environment, behind basic auth. The loop's reader key —
  which a manipulated fixer could read from the user environment — can mark a decision as
  consumed (a denial of one action) but cannot create or change one.
- A `ship` decision bypasses guard and review by design; it ships only the exact commit shown to
  the maintainer and still passes the fast gate when develop moved.
- Guidance text reaches the fixer as a trusted block. It does not lift the fixer's fixed rules
  (no pushes, no edits to protected paths, no weakened checks); the guard and review still run
  on a refix.

## Testing

- Bug API (Jest): maintainer key required and fail-closed; action validation; status and
  pending-decision `409`s; history entry; `decisionConsumed`; `review-diff` size limit;
  `decisionPending` / `awaitingDecision` filters; list excludes `reviewDiff`.
- Bug loop (unittest): park to `needs_decision` with design question + diff upload; review
  blockers for `design_question` / `guidance_followed`; each decision action incl. refix limit,
  ship refused when the branch moved, ship respecting breaker and freeze, discard; decision
  consumed before acting; guidance block present for the fixer and reviewer and absent
  otherwise; notifier (User-Agent, truncation, swallowed failures, no URL in logs, no player
  text); daily summary sent once per day; webhook and UI URL scrubbed from stage env.
- Web UI (Jest): query building for the new filters; decision panel state logic (pending
  disables buttons, refix requires guidance) as pure functions; production build.
- Proxy: `nginx -t` on the rendered template in the image build check, and a grep that only the
  decision location uses `BUG_MAINTAINER_KEY`.

## Rollout

1. Bug API: deploy with `BUG_MAINTAINER_KEYS` set (new key, `openssl rand -hex 32`).
2. Web UI: deploy with `BUG_MAINTAINER_KEY` set to that key.
3. Bug loop: merge, push develop; set `MMO_BUGLOOP_WEBHOOK` (and optionally
   `MMO_BUGLOOP_UI_URL`) as user environment variables; restart the scheduled task so it picks
   up the variables and the new snapshot.
4. The two bugs already parked before this feature (`bugfix/6462d2bc`, `bugfix/6462d2be`) have
   no `reviewDiff`; the loop uploads it for every `pr_open`/`needs_decision` bug without one on
   its first poll after the update (from `artifacts/bug-loop/<id>/diff.patch`).
