---
description: Merge the current gated feature branch into develop. Refuses without a green, fresh, full gate report.
---

Merge the current feature branch into develop, gated by `tools/gate/last_report.json`.

## Step 0

Record the current branch name first — every `<branch>` below means that recorded name.

## Preconditions — refuse (explain which check failed, tell the user to run /gate, and STOP) unless ALL hold:

1. Current branch is not `develop`.
2. Working tree is clean: `git status --porcelain` prints nothing.
3. `tools/gate/last_report.json` exists.
4. In the report: `passed` is `true` AND `e2e_skipped` is `false`.
5. The report's `commit` equals the output of `git rev-parse HEAD` (a gate run followed
   by any new commit invalidates the report — this is intentional).
6. The report's `steps` array contains an entry with `name` `"e2e"` and `passed` `true`.

## Merge (only when every precondition holds)

1. `git checkout develop`
2. `git merge --no-ff <branch> -m "Merge <branch> (gate green at <first 8 chars of report commit>)"`
3. `git branch -d <branch>`
4. Report the merge commit hash to the user.

## Hard rules

- NEVER push — pushing to origin stays a human decision.
- NEVER use `--force`, `git reset`, or history rewrites to satisfy a precondition.
- If the merge itself conflicts, abort it (`git merge --abort`), return to the feature
  branch, and report the conflict to the user instead of resolving it silently.
