---
description: Fetch new crash reports, symbolicate the oldest unhandled one, and attempt a fix on a branch (one per run).
---

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
are your starting points. Server reports (`login_server`, `realm_server`, `world_server`) symbolicate the same
way as client ones; when the build was never archived, the script falls back to a matching EXE + PDB pair in
the local `bin\<config>\` (a dev build that crashed on this machine). Reports from the Linux servers
(`pdbid=elf` in MODULES) resolve only against the ELF binary of the exact commit named in the report's
`Version:` line, placed at `artifacts\symbols\<module>.elf\<commit>\<module>`. The script fetches it from that
nightly's `server-symbols.zip` release asset when it is missing (`tools\fetch_server_symbols.ps1 -Commit <sha>`; needs
`gh`). A commit with no such release (older than the asset, or not a nightly): `fetch_server_symbols.ps1 -FromImage`
copies the binary out of the Docker image, which gives function names but no file:line.

Client reports (`mmo_client`) are identified by **version**, not commit — report.json's
`appVersion` (e.g. `0.3.0.4140`) names the published build. When no matching PDB is archived
locally, the script automatically runs `tools\fetch_client_symbols.ps1 -Version <appVersion>`,
which finds the GitHub release tagged for that version (`nightly-<version>`,
https://github.com/Kyoril/mmo/releases) and downloads its `symbols.zip` (PDBs) and
`client-bin.zip` (the EXE) assets, then archives the matching pair via `tools\archive_symbols.ps1`
(needs `gh`). This only works for builds that were actually published as a release — a crash from
an unreleased local build still needs a matching EXE+PDB in `bin\<config>\`, or a manual
`tools\archive_symbols.ps1 -Binary <path>`.

If nothing resolved (no matching PDB/EXE in `artifacts\symbols`, no local `bin\` match, and no
GitHub release for that version/commit — or `gh` itself is unavailable), the build that crashed
could not be symbolicated: document that in `FINDINGS.md`, set `STATUS` to `needs-info`, and stop
(tell the user which build is missing and how to supply it — either run `tools\archive_symbols`
on the matching build, or make sure `gh` is authenticated so the fetch can run next time).

## Step 4 — Investigate

Read `crash.txt` (exception type, full trace, PLAYER DATA, attached client log) and `report.json`
(appVersion, osVersion, user comment). Server reports are uploaded automatically by the next start of the crashed tier:
`report.json` has `errorMessage` "<tier> crash: <reason>" and `deviceModel` "<tier> @ <node>", and `crash.txt` holds
the report followed by the tail of that run's server log. Their `Context N:` lines name what the crashing thread
was doing, outermost first — the packet, the character (guid, name, map, instance) and the world instance tick —
and `Version:` names the exact build and commit. Open the resolved source files at the reported lines. Build a
concrete root-cause hypothesis: what was null/out-of-bounds/uninitialized, and under what conditions.
Cross-reference the client log tail for the sequence that led there.

## Step 5 — Attempt a fix (only if confident)

If you have a confident, minimal fix:

1. Create a branch: `git checkout -b crash-fix/<id-short>` (use the first 8 chars of the id).
2. Make the smallest correct change that addresses the root cause. Match surrounding code style
   (Allman braces, tabs, `m_camelCase`, PascalCase methods — see CLAUDE.md).
3. Best-effort compile check of the affected target:
   `cmake --build build --config Release -t mmo_client` for a client crash, or the crashed tier's target
   (`login_server`, `realm_server`, `world_server`) for a server crash (note success/failure; don't block on a slow
   build — if it's clearly unrelated or too long, say so).
4. `git add -A && git commit` the change (local only — **no push**) with a message referencing the
   crash and root cause. End the commit body with:
   `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`
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
