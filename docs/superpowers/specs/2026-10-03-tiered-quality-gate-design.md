# Tiered Quality Gate — Design

Date: 2026-10-03
Status: approved in brainstorming, pending spec review

## Problem

The local quality gate (`/gate` → `tools/gate/verify.ps1`, `/ship`) runs the full pipeline
on every merge to `develop`. A run takes ~8.5 min, of which E2E is ~7 min (417 s of ~500 s
in the last report), followed by a code-review subagent. `/ship` requires the report's
commit to equal HEAD, so every follow-up fix commit costs another full run.

Meanwhile the nightly gate, which should be the safety net, has not executed since at
least 2026-09-23: `nightly_gate.ps1` skips whenever `H:/mmo` is dirty or not on `develop`,
and with parallel sessions that is always the case at 03:00.

## Goal

Put the expensive checks where they pay off:

| Tier | When | Runs | Cost |
|---|---|---|---|
| Fast | every merge to develop (`/ship`) | protocol check, tool tests, build, unit tests, serialization warning | ~1.5 min incremental |
| Full | nightly on develop, `/gate full` on demand | fast tier + E2E (+ code review when on demand) | ~8.5 min |
| Release | before publishing a build to live | green full report for the exact develop commit | 0 if nightly covered it, else ~8.5 min |

## 1. Merge tier

**`verify.ps1`** keeps its steps. Add an explicit `-Tier fast|full` parameter (default
`full` so the nightly and release paths stay unchanged); `fast` equals today's `-SkipE2E`.
`-SkipE2E` stays as an alias. The report gets a `tier` field (`"fast"`/`"full"`) in addition
to `e2e_skipped`.

**`/gate`** (`.claude/commands/gate.md`):
- Default: `verify.ps1 -Tier fast`, then `serialization_warning.py`; print its output if any
  and flag it as needing judgement. No code-review subagent.
- `/gate full`: `verify.ps1 -Tier full`, serialization warning, then the code review of
  `git diff develop...HEAD` exactly as today.
- Red handling is unchanged (quote the failing step log; E2E transcript tail for `e2e`).

**`/ship`** (`.claude/commands/ship.md`):
- Preconditions: not on `develop`; clean tree.
- If `last_report.json` is missing, red, or its `commit` ≠ HEAD, run `/gate`'s fast tier
  first. Red → stop and report as `/gate` would. Green → continue.
- Either a fast or a full green report for HEAD satisfies it. The "must contain a green
  `e2e` step" precondition is removed.
- Merge mechanics, worktree handling, no-push and no-force rules unchanged.

## 2. Nightly tier

**Dedicated worktree** at `H:/mmo-nightly` (outside `.claude/worktrees`, so session cleanup
never touches it). Sessions must not use it; CLAUDE.md says so.

**`nightly_gate.ps1`** rewritten to operate on that worktree instead of skipping:
1. Create the worktree (detached) if it does not exist.
2. `git checkout --detach develop` there (develop's current commit; works while develop is
   checked out elsewhere).
3. `git -c protocol.file.allow=always submodule update --init` — all submodules, including
   both `data/*`, which E2E needs.
4. If `build/` is missing, run the CMake configure mirroring `H:/mmo/build/CMakeCache.txt`'s
   `MMO_*` options and generator.
5. If develop's commit equals the last nightly's commit and that run was green, write a
   report with `"unchanged": true` and stop (no wasted 8 min).
6. Run `verify.ps1 -Tier full` in the worktree.
7. Write `tools/gate/reports/nightly-YYYY-MM-DD.json` in the **main** checkout's
   `tools/gate/reports/` (where sessions look), containing the gate report plus:
   - `last_green_commit`: commit of the most recent green nightly report.
   - `merges_since_last_green`: `git log --first-parent --oneline <last_green>..<commit>`
     lines — on red, the list of suspects.

The "repo busy → skipped" path is removed. A run that cannot start (worktree/submodule/
configure failure) writes a red report with a `setup_error` field rather than silently
skipping.

**Session start rule** (CLAUDE.md): surface the newest nightly report if it is red, **or**
if no nightly report with a non-null `passed` is younger than 48 h (the nightly is dead).

**First execution** happens during implementation, triggered via
`Start-ScheduledTask "MMO Nightly Gate"` (not from a Claude shell), to prove it works in the
Task Scheduler context — in particular that `python` with `protobuf` is launchable there,
which is known to fail from Claude sessions.

## 3. Release tier

**`tools/gate/release_check.ps1`** — the single source of truth, so a future publish script
can call it and act on the exit code:
- Argument: commit-ish, default `develop`.
- Exit 0 if a green report with `tier: full` exists for that exact commit: either the newest
  nightly report for it or the nightly worktree's `last_report.json`.
- Otherwise, unless `-NoRun`, runs the full gate on that commit in `H:/mmo-nightly` (same
  setup steps as the nightly), copies the report to `tools/gate/reports/release-<sha8>.json`,
  and exits with its result.
- Prints the commit, report path and verdict.

**`/release`** (`.claude/commands/release.md`): runs `release_check.ps1`, reports
GREEN ("safe to publish <sha8>") or RED with the failing step, exactly like `/gate`. It
never publishes, pushes or tags — publishing stays manual for now.

## Out of scope

- Automating the publish itself (Release build, client distribution, Docker images). The
  user wants this mid-term; it is a separate project that will call `release_check.ps1`.
- The submodule-pointer-not-on-origin check (known gap, unrelated to speed).
- Changing what the steps test.

## Docs

- CLAUDE.md "Agentic Workflow" section: describe the three tiers, the `H:/mmo-nightly`
  worktree being off-limits, `/release`, and the updated session-start rule.
- Memory `agentic-dev-flow-gate.md`: update to the tiered model.

## Testing

All changes are PowerShell scripts and command docs; they are verified by running them:
  - `/gate` on this branch → fast report, `tier: fast`.
  - `/ship` precondition logic dry-checked against a stale report.
  - `release_check.ps1 -NoRun` on a commit without a report → non-zero; with a report → 0.
  - One real nightly run via Task Scheduler, inspected in `tools/gate/reports/`.
