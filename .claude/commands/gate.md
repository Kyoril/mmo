---
description: Run the local quality gate. Fast tier by default (build + unit tests); "/gate full" adds E2E and a code review.
---

Run the local quality gate for the current branch. Argument: `full` (optional).

## Steps

1. Run the gate:
   - Default: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -Tier fast`
     (protocol check, build, unit tests, tool tests — ~1.5 min incremental).
   - `/gate full`: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -Tier full`
     (adds E2E, ~8.5 min). Set `$env:MMO_E2E_MYSQL_PASSWORD` first, or the e2e step fails
     with `"log": null`.
2. **If the gate is RED** (non-zero exit): read `tools/gate/last_report.json`, identify
   the failing step, and quote the relevant lines from its log under `tools/gate/logs/`.
   For an `e2e` failure, also read `e2e/runtime/logs/summary.json` and quote the failing
   scenario's transcript tail from `e2e/runtime/logs/<scenario>.jsonl`. If the `e2e` step
   has `"log": null` and `exit_code` -1, E2E never ran because `MMO_E2E_MYSQL_PASSWORD` is
   not set — report exactly that. A failing `protocol` step usually means the wire format
   changed without a version bump; its log names the constant and the exact command to run,
   so quote that. It can also fail because the manifest is corrupt or python is not
   launchable (`$env:MMO_GATE_PYTHON` overrides it), so read the log rather than assuming.
   A failing `protocol_tests` step means the checker itself is broken.
   Summarize the failure and STOP.
3. **If the gate is GREEN** and the current branch is not `develop`:
   a. Run `python tools/gate/serialization_warning.py` (advisory, always exits 0, prints
      nothing when no packet serialization changed). If it prints, show it and say whether
      any listed edit looks like a wire-format change without a protocol version bump.
   b. Only for `/gate full`: dispatch a code review of `git diff develop...HEAD` using the
      superpowers:requesting-code-review skill with base `develop`, passing the
      serialization warning output along if there was any.
4. Final summary: a small table of gate steps (name, result, duration from the report),
   the tier, and any findings that need action.

## Hard rules

- Never merge from this command — merging is /ship's job.
- Never run /code-review ultra (user-triggered and billed).
- Never push.
