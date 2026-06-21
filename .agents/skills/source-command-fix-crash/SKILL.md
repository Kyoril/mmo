---
name: "source-command-fix-crash"
description: "Fetch new crash reports, symbolicate the oldest unhandled one, and attempt a fix on a branch (one per run)."
---

# source-command-fix-crash

Use this skill when the user asks to run the migrated source command `fix-crash`.

## Command Template

You are working through the crash-report fix loop for this MMO engine. Do **one** crash report this
run, end to end, then stop. Be rigorous and conservative: a wrong "fix" is worse than no fix.

## Hard rules

- Work on **one** report only this run (the oldest unhandled one).
- Do **not** push, open PRs, merge, or delete anything on the crash-report server. The server's
  DELETE endpoint is off-limits — the user reviews everything.
- Make code changes only on a dedicated branch; leave `develop` clean when you finish.
- Everything you produce as state goes under that report's folder in `artifacts/crash-reports/<id>/`
  (gitignored). Never commit anything under `artifacts/`.
- If you cannot confidently identify a root cause, do **not** guess-edit code. Document findings and
  mark the report `needs-info`.

## Step 1 — Sync new reports

Run the fetch script (downloads only reports not already present locally):

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\sync_crash_reports.ps1 -Json
```

The local folder is the source of truth / backup. Note the `JSON_SUMMARY` line.

## Step 2 — Pick one report

List `artifacts/crash-reports/*/STATUS`. Choose the **oldest** report directory whose `STATUS` is
`downloaded` (ignore `analyzed`, `fix-attempted`, `fixed`, `wont-fix`, `needs-info`, `download-failed`).
Prefer ordering by the report's `createdAt` in `report.json`, falling back to directory name.

If there is no `downloaded` report, write nothing, report "No new crash reports to work on," and stop.

## Step 3 — Symbolicate

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\symbolicate_crash.ps1 -ReportDir artifacts\crash-reports\<id>
```

Read `symbolized.txt`. The "OUR SOURCE LOCATIONS" section lists frames resolved under `src/` — those
are your starting points. If nothing resolved (no matching PDB/EXE in `artifacts\symbols`), the build
that crashed was never archived: document that in `FINDINGS.md`, set `STATUS` to `needs-info`, and stop
(tell the user to run `tools\archive_symbols` on the matching build).

## Step 4 — Investigate

Read `crash.txt` (exception type, full trace, PLAYER DATA, attached client log) and `report.json`
(appVersion, osVersion, user comment). Open the resolved source files at the reported lines. Build a
concrete root-cause hypothesis: what was null/out-of-bounds/uninitialized, and under what conditions.
Cross-reference the client log tail for the sequence that led there.

## Step 5 — Attempt a fix (only if confident)

If you have a confident, minimal fix:

1. Create a branch: `git checkout -b crash-fix/<id-short>` (use the first 8 chars of the id).
2. Make the smallest correct change that addresses the root cause. Match surrounding code style
   (Allman braces, tabs, `m_camelCase`, PascalCase methods — see AGENTS.md).
3. Best-effort compile check of the affected target:
   `cmake --build build --config Release -t mmo_client` (note success/failure; don't block on a slow
   build — if it's clearly unrelated or too long, say so).
4. `git add -A && git commit` the change (local only — **no push**) with a message referencing the
   crash and root cause. End the commit body with:
   `Co-Authored-By: Codex Opus 4.8 <noreply@anthropic.com>`
5. `git checkout develop` so the tree is clean for the next run. The fix stays on its branch for the
   user to review.

## Step 6 — Document and set status

Write `artifacts/crash-reports/<id>/FINDINGS.md` containing:

- **Summary** — one line: exception + where it crashed.
- **Symbolicated trace** — the resolved `src/` frames.
- **Root cause** — your hypothesis and the evidence (code + log) for it.
- **Fix** — what you changed and why (with file:line), the branch name, and the compile result; or
  why you did not change code.
- **Confidence** — high / medium / low, and what would raise it.
- **Repro / context** — player state, version, and the log sequence leading to the crash.

Then set `STATUS` to one of: `fix-attempted` (code changed on a branch), `wont-fix` (analyzed, no
change warranted), or `needs-info` (couldn't symbolicate or root-cause).

## Step 7 — Report back

Give a short summary: which report id, the root cause, what you did (branch name if any, compile
result), confidence, and what the user should review.
