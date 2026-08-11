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
   scenario's transcript tail from `e2e/runtime/logs/<scenario>.jsonl`. Exception: if the
   `e2e` step has `"log": null` and `exit_code` -1, E2E never ran because
   `MMO_E2E_MYSQL_PASSWORD` is not set — report exactly that instead of hunting for logs.
   A failing `protocol` step usually means the wire format changed without a version bump;
   its log names the constant and the exact command to run, so quote that rather than
   guessing a fix. It can also fail because the manifest is corrupt or `python` is not on
   PATH, so read the log rather than assuming. A failing `protocol_tests` step means the
   checker itself is broken — its verdict cannot be trusted until that is fixed.
   Summarize the failure and STOP — no review, no merge.
3. **If the gate is GREEN** and the current branch is not `develop`:
   a. Run `python tools/gate/serialization_warning.py`. It is advisory and always exits 0;
      it prints nothing when the branch changed no packet serialization. If it does print,
      pass its output to the reviewer in step (b) and ask specifically whether any of the
      listed edits change a packet's wire format without a protocol version bump.
   b. Dispatch a code review of `git diff develop...HEAD` using the
      superpowers:requesting-code-review skill with base `develop`.
   (On `develop` itself there is nothing to review — skip both.)
4. Final summary: a small table of gate steps (name, result, duration from the report),
   then the review findings that need action, if any. Recommend fixes for serious
   findings before shipping.

## Hard rules

- Never merge from this command — merging is /ship's job.
- Never run /code-review ultra (user-triggered and billed).
- Never push.
