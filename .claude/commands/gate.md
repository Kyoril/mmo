---
description: Run the local quality gate (build + unit tests + E2E), then a code review of the branch diff if green.
---

Run the local quality gate for the current branch, then review the branch diff.

## Steps

1. Run the gate:
   `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1`
   Append `-SkipE2E` ONLY if the user explicitly asked for a quick pre-check — a
   skipped-E2E report is not valid for /ship, say so in the summary.
2. **If the gate is RED** (non-zero exit): read `tools/gate/last_report.json`, identify
   the failing step, and quote the relevant lines from its log under `tools/gate/logs/`.
   For an `e2e` failure, also read `e2e/runtime/logs/summary.json` and quote the failing
   scenario's transcript tail from `e2e/runtime/logs/<scenario>.jsonl`. Summarize the
   failure and STOP — no review, no merge.
3. **If the gate is GREEN** and the current branch is not `develop`: dispatch a code
   review of `git diff develop...HEAD` using the superpowers:requesting-code-review
   skill with base `develop`. (On `develop` itself there is nothing to review — skip.)
4. Final summary: a small table of gate steps (name, result, duration from the report),
   then the review findings that need action, if any. Recommend fixes for serious
   findings before shipping.

## Hard rules

- Never merge from this command — merging is /ship's job.
- Never run /code-review ultra (user-triggered and billed).
- Never push.
