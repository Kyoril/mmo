# Agentic Development Flow — Design

**Date:** 2026-07-24
**Status:** Approved

## Goal

Give agent sessions more autonomy (fewer permission prompts, recurring automation) while
adding quality gates so agent-produced code cannot land on `develop` unverified. All
verification runs locally on the Windows dev machine; CI stays unchanged as a backstop.

## Scope decisions (agreed)

- **Gate location:** local only. No self-hosted CI runner, no PR requirement.
- **Branch discipline:** all agent implementation work happens on `feature/<topic>`
  branches; merge to `develop` only through the gate. Direct commits to `develop` are
  reserved for trivial data/docs tweaks the user explicitly requests.
- **Mechanisms in scope:** permission allowlist, scheduled/background jobs, review
  subagent. Hooks are explicitly out of scope (user declined).
- **Scheduled jobs:** nightly gate run on `develop` + weekly content-integrity audit.
  No crash-fix sweep (user declined; `/fix-crash` remains manual).

## Components

### 1. Gate script — `tools/gate/verify.ps1`

Single deterministic entry point; no AI in the verification loop. Steps, in order,
fail-fast:

1. **Build** — `cmake --build build --config Debug` for: `login_server`, `realm_server`,
   `world_server`, `e2e_client`, `unit_tests`, `game_server_unit_tests`,
   `login_server_tests`. Debug config because the E2E harness requires
   `MMO_WITH_DEV_COMMANDS=ON`.
2. **Unit tests** — run the three test binaries, capturing output to log files.
3. **E2E** — invoke `tools/e2e/e2e_run.ps1`. Fails early with a clear message if
   `MMO_E2E_MYSQL_PASSWORD` is not set.
4. **Report** — write `tools/gate/last_report.json` (gitignored) containing: branch,
   HEAD commit hash, timestamp, per-step pass/fail with pointers to step logs, overall
   verdict. Exit code 0 only if every step passed.

Flags:
- `-SkipE2E` — quick pre-check (report records that E2E was skipped; a skipped-E2E
  report is **not** valid for `/ship`).
- `-Targets` — override the build target list for niche cases.

On failure the report records which step failed and where its log lives (E2E logs are
already in `e2e/runtime/logs/`).

### 2. Project skill — `/gate`

1. Runs `tools/gate/verify.ps1`.
2. If red: summarize the failing step with log excerpts; stop.
3. If green: dispatch a code-review subagent over `git diff develop...HEAD` using the
   superpowers code-review flow (not `/code-review ultra`, which is user-triggered and
   billed).
4. Final output: combined summary — gate verdict + actionable review findings.

### 3. Project skill — `/ship`

1. Read `tools/gate/last_report.json`.
2. Refuse unless: overall verdict is pass, E2E was not skipped, and the recorded commit
   hash equals current `HEAD` (prevents gate → extra commit → merge).
3. If valid: merge the feature branch into `develop` with `--no-ff`, then delete the
   branch.
4. Never pushes — pushing to origin stays a human decision.

### 4. CLAUDE.md — "Agentic workflow" section

Short section documenting: branch-first convention (`feature/<topic>`, worktrees
optional for parallel sessions), the `/gate` → `/ship` merge path, and the
trivial-change exception for direct `develop` commits.

### 5. Permission allowlist — `.claude/settings.json`

Extend `permissions.allow` with:
- `cmake --build build` variants
- the three test binaries
- `tools/gate/verify.ps1` and `tools/e2e/e2e_run.ps1`
- skill scripts (`python .claude/skills/...`)
- git: status/diff/log/branch/checkout/add/commit/merge

Explicitly **not** allowlisted: `git push`, destructive git (`reset --hard`, `clean`),
anything rewriting `develop` history.

### 6. Scheduled jobs (local machine)

- **Nightly gate on develop** — run `verify.ps1` against a clean `develop` checkout;
  report failures so regressions that slip past a merge gate surface within a day.
- **Weekly content audit** — run the quest/item/NPC/spell skill validators plus
  `tools/xp_audit.py` over live game data; report broken references and XP coverage
  below the 120% threshold.

## Error handling

- `verify.ps1` is fail-fast; every failure names the step and its log path.
- `/ship` treats a missing, stale (wrong commit), red, or E2E-skipped report as a hard
  refusal with an explanation of what to do (`/gate` again).
- Scheduled jobs must not fight an active dev session: the E2E stack already uses its
  own ports and throwaway databases, so a nightly run coexists with a running dev stack.

## Rollout verification

1. Green path: trivial change on a test branch → `/gate` → `/ship` → confirm merge on
   `develop`.
2. Red path: deliberately break one unit test on another branch → confirm `/gate` fails
   and `/ship` refuses.

## Out of scope / future

- Hooks-based hard enforcement (declined for now).
- Self-hosted Windows CI runner + PR flow (possible later; habits transfer unchanged).
- Crash-fix sweep automation (`/fix-crash` stays manual).
- The gate flow was verified end-to-end on 2026-07-24.
