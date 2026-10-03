---
description: Check that a develop commit passed the full gate (build, unit tests, E2E) before it is published to live. Never publishes.
---

Release tier of the quality gate. Publishing to the live client distribution / servers is
manual for now; this command answers "is this commit safe to publish?".

## Steps

1. Make sure `$env:MMO_E2E_MYSQL_PASSWORD` is set in the shell (see e2e/README.md and the
   e2e-test-harness memory) — the check may have to run E2E.
2. Run:
   `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/release_check.ps1`
   Append `-Ref <commit-ish>` if the user named a commit or tag; the default is `develop`.
   If no nightly covered the commit this runs the full gate in `H:/mmo-nightly` and takes
   ~10 min — run it in the background and say so.
3. Exit 0: report "safe to publish <sha8>" and which report proved it.
   Exit 1: read the `release-<sha8>.json` named in the output, identify the failing step,
   quote the relevant lines of its log under `H:/mmo-nightly/tools/gate/logs/` (for `e2e`
   also the failing scenario's tail from `H:/mmo-nightly/e2e/runtime/logs/`). If the output
   says the worktree could not be prepared, quote that error instead.
   Exit 2: the ref does not exist.

## Hard rules

- Never publish, push or tag from this command.
- Never edit files in `H:/mmo-nightly` — the gate force-checks it out on every run.
- Do not run it while another session is running E2E (shared test ports).