# Bug Loop — Implement as Feature — Design

Date: 2026-10-08
Status: approved in brainstorming, awaiting spec review
Builds on: `docs/superpowers/specs/2026-10-07-bug-loop-decisions-design.md`, `docs/bug-loop.md`

## Goal

Some player reports are rightly rejected by the bug loop: nothing in the project says the game
should behave the way the player wants, so they end as `not_a_bug` (status `wontfix`) or
`design_request` (status `triaged`). The maintainer may still like the idea. With one action in
the web UI ("Implement as feature") and a description of the wanted behaviour, the maintainer
turns such a report into a feature request that the loop implements like a fix. The maintainer's
description replaces "the project" as the source of the expected behaviour.

Features never ship on their own: every implemented feature is parked for the maintainer, who
then uses the existing actions (ship as is, refix with guidance, discard).

## Non-goals

- Feature requests that did not start as a player report (a free-form "new feature" form).
- Turning abuse-flagged reports into features. Never offered, refused by the API.
- Changing the auto-ship rules for bug fixes.

## Bug API

- Decision actions gain `implement`. Body `{ action: 'implement', guidance }`; guidance is
  required (non-empty after trim, ≤ 4000 chars) and is the feature description.
- `implement` is accepted only for:
  - status `wontfix` with `triage.category = 'not_a_bug'`, or
  - status `triaged` with `triage.category = 'design_request'`.
  Everything else (including `triage.category = 'abuse'`) answers `409`. The existing actions keep
  their rule (status `pr_open` or `needs_decision`).
- The one-pending-decision rule and the maintainer-key requirement apply unchanged.
- History: `decision: implement: <description>` with actor `maintainer`.

## Bug loop

- `_handle_decisions` handles `implement` (live mode only, like the other actions):
  - needs 2 invocations of budget (else it stays pending), consumes the decision first;
  - refuses an empty description like a refix without guidance (status unchanged, note
    "implement needs a description; decide again");
  - needs the bug's `report.json` and `triage.json` artifacts (written at triage); without them
    it notes "cannot implement: triage artifacts missing" and stops;
  - sets `triage.category = 'feature'` (note "accepted as a feature by the maintainer") and
    records the bug id in a persisted `features` set in the loop state;
  - runs the normal fix path on a fresh `bugfix/<id8>` branch from `origin/develop` with the
    description as a trusted `FEATURE REQUEST` block (provenance "maintainer decision via the
    web UI; trusted and binding").
- Fixer (`prompts/fix.md`): a FEATURE REQUEST block defines the expected behaviour; cite it as
  `expected_source` ("maintainer feature decision"); `no_project_basis` is not a valid outcome for
  a feature. All fixed rules stay (never push, protected paths, never weaken checks or make rewards
  more generous); if the feature would need that, outcome `no_root_cause` with an explanation.
- Reviewer: gets the FEATURE REQUEST block; `expected_source_supported` and `guidance_followed`
  are judged against it.
- Guard, regression proof and full gate run unchanged.
- Never auto-ship: for any bug in `features`, `_verify_and_ship` adds the reason
  "feature: shipping needs the maintainer's approval" and parks (status `pr_open`). The same holds
  for later guided refixes of a feature. A Discord message "Feature ready for review" (with link
  and branch) goes out when a feature parks for the first time with an otherwise green result.
- Failure paths during an implement run release to `pr_open` when a branch exists, else back to
  the bug's previous status with a note, so the maintainer can always decide again.

## Web UI

- Bug page: for the two eligible states a "Feature request" panel shows the triage reasoning (why
  it was rejected), a "Describe the behaviour you want" field and an **Implement as feature**
  button (confirm dialog: "The bug loop implements this as a feature. It will not ship without
  your approval."). It shows the pending state like the decision panel.
- A `Feature` chip next to the status for bugs with `triage.category = 'feature'` (list and page).
- The status list filter stays; no new filter.

## Security notes

- Same trust model as the other decisions: only the maintainer key creates the decision; the
  description is trusted but cannot lift the fixer's fixed rules; the guard still runs; abuse
  reports are excluded at the API.
- Because features always park, the existing hash-bound "ship as is" is the only way a feature
  reaches develop.

## Testing

- API: `implement` accepted for the two eligible states, `409` for others and for abuse, `400`
  without a description, maintainer key required.
- Loop: implement runs on a fresh branch with the FEATURE REQUEST block for fixer and reviewer;
  always parks even when everything is green; the Discord message; empty description refused;
  missing artifacts; refix of a feature still never auto-ships; dry run ignores it; budget.
- UI: eligibility logic as a pure function with tests; the panel only for eligible bugs; build.

## Rollout

Same order as the decisions feature: deploy the API and UI first, then push develop; the loop
picks the change up at its next restart (or re-register and restart the task).
