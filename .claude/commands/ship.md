---
description: Merge the current feature branch into develop after a green fast gate (runs it if needed). Never pushes.
---

Merge the current feature branch into develop. The merge tier is the fast gate (protocol
check, build, unit tests, tool tests). E2E runs nightly on develop and before releases
(/release), not here.

## Step 0

Record the current branch name — every `<branch>` below means that name.

## Preconditions — refuse (say which check failed) and STOP unless both hold:

1. Current branch is not `develop`.
2. `git status --porcelain` prints nothing. (The gate stamps HEAD; uncommitted work would
   ride along unchecked.) Submodule pointer changes must be committed too.

## Gate

1. Read `tools/gate/last_report.json`. It satisfies the merge if it exists, `passed` is
   `true`, and `commit` equals `git rev-parse HEAD` — fast or full tier.
2. Otherwise run `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -Tier fast`.
   If it is red, report it exactly as /gate does (failing step, quoted log lines) and STOP.
3. Run `python tools/gate/serialization_warning.py`. If it prints anything, show it and ask
   the user whether the listed edits change a packet's wire format without a protocol
   version bump; merge only after they answer.

## Merge

1. Find the checkout holding develop: in `git worktree list --porcelain`, the `worktree`
   line of the block containing `branch refs/heads/develop`.
   - Held by another checkout `<X>`: if `<X>` is not `H:/mmo` and may belong to a live
     session, ask the user before merging there. Then
     `git -C <X> merge --no-ff <branch> -m "Merge <branch> (gate green at <first 8 chars of report commit>)"`
     and `git checkout --detach` in the current checkout.
   - Held by nobody: `git checkout develop` here, then the same `git merge --no-ff ...`.
     If the current checkout is a `.claude/worktrees/*` worktree, `git checkout --detach`
     afterwards so develop is free for other checkouts.
2. If the branch changed a submodule pointer, run
   `git -C <checkout that merged> -c protocol.file.allow=always submodule update` so that
   checkout is not left dirty.
3. `git branch -d <branch>`
4. Report the merge commit hash, and remind the user that E2E for it runs in tonight's
   nightly gate (or now via /release if they intend to publish).

## Hard rules

- NEVER push — pushing to origin stays a human decision.
- NEVER use `--force`, `git reset`, or history rewrites to satisfy a precondition.
- If the merge conflicts, `git merge --abort` in that checkout, return to the feature
  branch, and report the conflict instead of resolving it silently.
