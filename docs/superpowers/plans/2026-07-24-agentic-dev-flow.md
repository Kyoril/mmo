# Agentic Development Flow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A local quality-gate pipeline (build + unit tests + E2E + review) that protects `develop` from unverified agent work, plus a permission allowlist and two scheduled maintenance jobs.

**Architecture:** A deterministic PowerShell gate script (`tools/gate/verify.ps1`) produces a machine-readable report; two slash commands (`/gate`, `/ship`) orchestrate verification, review, and gated merges around that report; Windows Task Scheduler runs the gate nightly on `develop` and a Python content-integrity audit weekly.

**Tech Stack:** PowerShell 5.1-compatible scripts, Python 3.12 (reusing the existing skill scripts + `proto_runtime`), Claude Code slash commands (`.claude/commands/*.md`), Windows Task Scheduler (`schtasks`).

**Spec:** `docs/superpowers/specs/2026-07-24-agentic-dev-flow-design.md`

## Global Constraints

- Copyright header on every new source file: `# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- PowerShell scripts: tabs for indentation, Allman braces (match `tools/e2e/e2e_run.ps1`); must run under Windows PowerShell 5.1 (`powershell`, not `pwsh`) because Task Scheduler invokes `powershell`.
- Python scripts: 4-space indentation (match `tools/xp_audit.py`).
- Build config for the gate is always `Debug` (the E2E harness requires `MMO_WITH_DEV_COMMANDS=ON`).
- Test binaries live in `bin/Debug/`; test binaries are run from the repo root.
- Nothing in this plan ever runs `git push`.
- Commit after every task, directly on `develop` (the gated-branch flow starts *after* this infrastructure exists).

---

### Task 1: Gate script `tools/gate/verify.ps1`

**Files:**
- Create: `tools/gate/verify.ps1`
- Modify: `.gitignore` (append gate output paths)

**Interfaces:**
- Produces: `tools/gate/last_report.json` with this exact schema (consumed by Tasks 2, 3, 6):
  ```json
  {
    "branch": "feature/x",
    "commit": "<full sha of HEAD at run time>",
    "timestamp": "<ISO 8601>",
    "config": "Debug",
    "e2e_skipped": false,
    "steps": [
      { "name": "build", "passed": true, "exit_code": 0, "log": "tools/gate/logs/build.log", "duration_s": 123.4 }
    ],
    "passed": true
  }
  ```
  Step names in order: `build`, `unit_tests`, `game_server_unit_tests`, `login_server_tests`, `e2e`. Fail-fast: later steps are absent from `steps` if an earlier one failed. Exit code 0 iff `passed` is true.

- [ ] **Step 1: Write the script**

Create `tools/gate/verify.ps1`:

```powershell
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Local quality gate: build -> unit tests -> E2E. Writes tools/gate/last_report.json
# and exits 0 only if every step passed. /ship refuses to merge into develop without
# a green, fresh (HEAD-matching), non-E2E-skipped report.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -SkipE2E   # quick pre-check; NOT valid for /ship

[CmdletBinding()]
param(
	[switch]$SkipE2E,
	[string[]]$Targets = @("login_server", "realm_server", "world_server", "e2e_client", "unit_tests", "game_server_unit_tests", "login_server_tests")
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$logDir = Join-Path $PSScriptRoot "logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

$steps = @()
$allPassed = $true

function Invoke-GateStep
{
	param(
		[string]$Name,
		[string]$Exe,
		[string[]]$Arguments = @()
	)

	$log = Join-Path $script:logDir ($Name + ".log")
	Write-Host ("== {0} ==" -f $Name)
	$sw = [System.Diagnostics.Stopwatch]::StartNew()
	# Out-Host keeps the command output off the pipeline so the function returns ONLY the boolean.
	& $Exe @Arguments 2>&1 | Tee-Object -FilePath $log | Out-Host
	$exit = $LASTEXITCODE
	$sw.Stop()

	$script:steps += [ordered]@{
		name = $Name
		passed = ($exit -eq 0)
		exit_code = $exit
		log = ("tools/gate/logs/{0}.log" -f $Name)
		duration_s = [math]::Round($sw.Elapsed.TotalSeconds, 1)
	}
	if ($exit -ne 0)
	{
		$script:allPassed = $false
	}
	return ($exit -eq 0)
}

$branch = (& git -C $repoRoot rev-parse --abbrev-ref HEAD)
$commit = (& git -C $repoRoot rev-parse HEAD)

Push-Location $repoRoot
try
{
	$ok = Invoke-GateStep -Name "build" -Exe "cmake" -Arguments (@("--build", "build", "--config", "Debug", "-t") + $Targets)

	if ($ok)
	{
		foreach ($test in @("unit_tests", "game_server_unit_tests", "login_server_tests"))
		{
			if (-not (Invoke-GateStep -Name $test -Exe (Join-Path $repoRoot ("bin\Debug\{0}.exe" -f $test))))
			{
				$ok = $false
				break
			}
		}
	}

	if ($ok -and -not $SkipE2E)
	{
		if (-not $env:MMO_E2E_MYSQL_PASSWORD)
		{
			Write-Host "MMO_E2E_MYSQL_PASSWORD is not set - cannot run E2E." -ForegroundColor Red
			$steps += [ordered]@{ name = "e2e"; passed = $false; exit_code = -1; log = $null; duration_s = 0 }
			$allPassed = $false
		}
		else
		{
			$null = Invoke-GateStep -Name "e2e" -Exe "powershell" -Arguments @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $repoRoot "tools\e2e\e2e_run.ps1"))
		}
	}
}
finally
{
	Pop-Location

	[ordered]@{
		branch = $branch
		commit = $commit
		timestamp = (Get-Date -Format "o")
		config = "Debug"
		e2e_skipped = [bool]$SkipE2E
		steps = $steps
		passed = $allPassed
	} | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $PSScriptRoot "last_report.json") -Encoding UTF8
}

Write-Host ""
Write-Host ("Gate result: {0} (report: tools/gate/last_report.json)" -f $(if ($allPassed) { "GREEN" } else { "RED" }))
exit $(if ($allPassed) { 0 } else { 1 })
```

- [ ] **Step 2: Gitignore the gate output**

Append to `.gitignore`:

```
tools/gate/last_report.json
tools/gate/logs/
tools/gate/reports/
```

- [ ] **Step 3: Run the quick path and verify the report**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -SkipE2E`
Expected: build + three test steps run, exit code 0, console ends with `Gate result: GREEN`.

Then inspect `tools/gate/last_report.json`: `passed` is `true`, `e2e_skipped` is `true`, `commit` equals `git rev-parse HEAD`, four steps present, each with a `log` path that exists under `tools/gate/logs/`.

- [ ] **Step 4: Verify the failure path**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -SkipE2E -Targets @("no_such_target")`
Expected: exit code 1, `Gate result: RED`, report has `passed: false` with only the failed `build` step (fail-fast: no test steps).

- [ ] **Step 5: Commit**

```bash
git add tools/gate/verify.ps1 .gitignore
git commit -m "Add local quality gate script (build + unit tests + E2E -> report)"
```

---

### Task 2: `/gate` command

**Files:**
- Create: `.claude/commands/gate.md`

**Interfaces:**
- Consumes: `tools/gate/verify.ps1` and its `last_report.json` schema (Task 1).
- Produces: the `/gate` slash command. `/ship` (Task 3) assumes `/gate` is the only intended producer of green reports.

- [ ] **Step 1: Write the command file**

Create `.claude/commands/gate.md`:

```markdown
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
```

- [ ] **Step 2: Verify the command loads**

Run: `head -5 .claude/commands/gate.md`
Expected: YAML frontmatter with the `description:` line (same shape as `.claude/commands/fix-crash.md`). Full slash-command behavior is exercised in Task 8.

- [ ] **Step 3: Commit**

```bash
git add .claude/commands/gate.md
git commit -m "Add /gate command: quality gate + branch-diff review"
```

---

### Task 3: `/ship` command

**Files:**
- Create: `.claude/commands/ship.md`

**Interfaces:**
- Consumes: `tools/gate/last_report.json` schema (Task 1): fields `passed`, `e2e_skipped`, `commit`.

- [ ] **Step 1: Write the command file**

Create `.claude/commands/ship.md`:

```markdown
---
description: Merge the current gated feature branch into develop. Refuses without a green, fresh, full gate report.
---

Merge the current feature branch into develop, gated by `tools/gate/last_report.json`.

## Preconditions — refuse (explain which check failed, tell the user to run /gate, and STOP) unless ALL hold:

1. Current branch is not `develop`.
2. Working tree is clean: `git status --porcelain` prints nothing.
3. `tools/gate/last_report.json` exists.
4. In the report: `passed` is `true` AND `e2e_skipped` is `false`.
5. The report's `commit` equals the output of `git rev-parse HEAD` (a gate run followed
   by any new commit invalidates the report — this is intentional).

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
```

- [ ] **Step 2: Verify the command loads**

Run: `head -5 .claude/commands/ship.md`
Expected: YAML frontmatter with the `description:` line. Full behavior (green accept, red refusal) is exercised in Task 8.

- [ ] **Step 3: Commit**

```bash
git add .claude/commands/ship.md
git commit -m "Add /ship command: report-gated merge into develop"
```

---

### Task 4: CLAUDE.md "Agentic Workflow" section

**Files:**
- Modify: `CLAUDE.md` (insert new section after "## Project Overview" / "## Implemented Gameplay Features", before "## Build Commands")

**Interfaces:**
- Consumes: `/gate` (Task 2), `/ship` (Task 3), report locations from Tasks 1 and 6/7.

- [ ] **Step 1: Insert the section**

Add to `CLAUDE.md`:

```markdown
## Agentic Workflow

- All agent implementation work happens on a `feature/<topic>` branch (use a worktree
  for parallel sessions). Direct commits to `develop` are reserved for trivial
  data/docs tweaks the user explicitly requests.
- Merging to `develop` goes through the local quality gate: `/gate` runs
  `tools/gate/verify.ps1` (Debug build + unit tests + E2E) and then a code review of
  the branch diff; `/ship` performs the merge and refuses without a green, full,
  HEAD-matching `tools/gate/last_report.json`.
- Never push to origin unless the user explicitly asks.
- Scheduled reports land in `tools/gate/reports/` (nightly gate on develop, weekly
  content audit). At session start, if the newest nightly report there is red,
  surface it to the user before starting new work.
```

- [ ] **Step 2: Commit**

```bash
git add CLAUDE.md
git commit -m "Document agentic workflow (feature branches, /gate, /ship) in CLAUDE.md"
```

---

### Task 5: Permission allowlist

**Files:**
- Modify: `.claude/settings.json`

- [ ] **Step 1: Replace the settings file**

Overwrite `.claude/settings.json` with (preserves the existing cmake entry and the `htex` MCP server):

```json
{
  "permissions": {
    "allow": [
      "Bash(cmake --build build:*)",
      "PowerShell(cmake --build build:*)",
      "Bash(powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1:*)",
      "PowerShell(powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1:*)",
      "Bash(powershell -NoProfile -ExecutionPolicy Bypass -File tools/e2e/e2e_run.ps1:*)",
      "PowerShell(powershell -NoProfile -ExecutionPolicy Bypass -File tools/e2e/e2e_run.ps1:*)",
      "Bash(powershell -File tools/e2e/e2e_run.ps1:*)",
      "Bash(./bin/Debug/unit_tests.exe:*)",
      "Bash(./bin/Debug/game_server_unit_tests.exe:*)",
      "Bash(./bin/Debug/login_server_tests.exe:*)",
      "Bash(python .claude/skills/:*)",
      "Bash(python tools/:*)",
      "Bash(git status:*)",
      "Bash(git diff:*)",
      "Bash(git log:*)",
      "Bash(git show:*)",
      "Bash(git rev-parse:*)",
      "Bash(git add:*)",
      "Bash(git commit:*)",
      "Bash(git branch:*)",
      "Bash(git checkout:*)",
      "Bash(git merge:*)",
      "Bash(git worktree:*)"
    ],
    "deny": [
      "Bash(git push:*)",
      "Bash(git reset --hard:*)",
      "Bash(git clean:*)"
    ]
  },
  "mcpServers": {
    "htex": {
      "command": "python",
      "args": ["F:/mmo/tools/mcp_htex/server.py"],
      "env": {
        "MMO_ROOT": "F:/mmo"
      }
    }
  }
}
```

Note: the old single allow entry `cmake --build build --target mmo_edit --config Debug` is subsumed by the `cmake --build build:*` prefix rule.

- [ ] **Step 2: Verify JSON validity**

Run: `python -c "import json; json.load(open('.claude/settings.json')); print('valid')"`
Expected: `valid`

- [ ] **Step 3: Commit**

```bash
git add .claude/settings.json
git commit -m "Allowlist gate/build/test/git commands; deny push and destructive git"
```

---

### Task 6: Nightly gate scheduled task

**Files:**
- Create: `tools/gate/nightly_gate.ps1`
- Create: `tools/gate/register_scheduled_tasks.ps1`

**Interfaces:**
- Consumes: `tools/gate/verify.ps1` (Task 1).
- Produces: `tools/gate/reports/nightly-YYYY-MM-DD.json` (either a copy of `last_report.json`, or `{skipped: true, reason: ...}` when the repo is busy). `register_scheduled_tasks.ps1` is extended by Task 7 — it must remain the single place where both schtasks registrations live.

- [ ] **Step 1: Write the nightly wrapper**

Create `tools/gate/nightly_gate.ps1`:

```powershell
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Nightly gate run on develop. Skips (without failing) when the repo is mid-work:
# dirty tree or a non-develop branch checked out. Copies the gate report to
# tools/gate/reports/nightly-YYYY-MM-DD.json so red nights are visible later.

[CmdletBinding()]
param(
	[switch]$SkipE2E
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$reportDir = Join-Path $PSScriptRoot "reports"
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
$target = Join-Path $reportDir ("nightly-{0}.json" -f (Get-Date -Format "yyyy-MM-dd"))

$branch = (& git -C $repoRoot rev-parse --abbrev-ref HEAD)
$dirty = (& git -C $repoRoot status --porcelain)

if ($branch -ne "develop" -or $dirty)
{
	[ordered]@{
		timestamp = (Get-Date -Format "o")
		skipped = $true
		reason = ("repo busy: branch={0}, dirty={1}" -f $branch, [bool]$dirty)
		passed = $null
	} | ConvertTo-Json | Set-Content -Path $target -Encoding UTF8
	Write-Host "Nightly gate skipped: repo busy."
	exit 0
}

$gateArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "verify.ps1"))
if ($SkipE2E)
{
	$gateArgs += "-SkipE2E"
}
& powershell @gateArgs
$exit = $LASTEXITCODE
Copy-Item (Join-Path $PSScriptRoot "last_report.json") $target -Force
exit $exit
```

- [ ] **Step 2: Write the registration script**

Create `tools/gate/register_scheduled_tasks.ps1`:

```powershell
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Registers the recurring maintenance jobs in Windows Task Scheduler (current user).
# Idempotent: /F overwrites existing definitions. Re-run after changing schedules.
#
# Requires MMO_E2E_MYSQL_PASSWORD as a persistent USER environment variable
# (setx MMO_E2E_MYSQL_PASSWORD "<password>"), otherwise the nightly E2E step fails.

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

if (-not [Environment]::GetEnvironmentVariable("MMO_E2E_MYSQL_PASSWORD", "User"))
{
	Write-Warning "MMO_E2E_MYSQL_PASSWORD is not set as a user environment variable; the nightly E2E step will fail until it is (setx MMO_E2E_MYSQL_PASSWORD `"<password>`")."
}

schtasks /Create /F /TN "MMO Nightly Gate" /SC DAILY /ST 03:00 `
	/TR ("powershell -NoProfile -ExecutionPolicy Bypass -File `"{0}\tools\gate\nightly_gate.ps1`"" -f $repoRoot)

schtasks /Create /F /TN "MMO Weekly Content Audit" /SC WEEKLY /D SUN /ST 04:00 `
	/TR ("python `"{0}\tools\gate\content_audit.py`"" -f $repoRoot)

Write-Host "Scheduled tasks registered. Verify with: schtasks /Query /TN `"MMO Nightly Gate`""
```

- [ ] **Step 3: Smoke-test the nightly wrapper's skip path**

The working tree is dirty right now (this plan's uncommitted edits) or on develop with staged files — first run:
`powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/nightly_gate.ps1 -SkipE2E`
Expected: if the tree is dirty → `Nightly gate skipped: repo busy.`, exit 0, and `tools/gate/reports/nightly-<today>.json` contains `"skipped": true`. If the tree is clean on develop → a full `-SkipE2E` gate run followed by the report copy. Either observed behavior is a pass; confirm the report file exists.

- [ ] **Step 4: Register the tasks**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/register_scheduled_tasks.ps1`
Expected: two `SUCCESS` lines from schtasks. (The weekly task points at `content_audit.py`, which Task 7 creates — registration order doesn't matter because the task only fires on Sundays; note this in the commit if Task 7 hasn't run yet.)

Then: `schtasks /Query /TN "MMO Nightly Gate"` and `schtasks /Query /TN "MMO Weekly Content Audit"`
Expected: both listed with status Ready.

If `MMO_E2E_MYSQL_PASSWORD` is not a persistent user env var (check: `[Environment]::GetEnvironmentVariable("MMO_E2E_MYSQL_PASSWORD", "User")`), ask the user to set it with setx (the value is in their e2e setup notes) — do not store the password in any committed file.

- [ ] **Step 5: Commit**

```bash
git add tools/gate/nightly_gate.ps1 tools/gate/register_scheduled_tasks.ps1
git commit -m "Add nightly develop gate + scheduled task registration"
```

---

### Task 7: Weekly content audit

**Files:**
- Create: `tools/gate/content_audit.py`

**Interfaces:**
- Consumes: skill scripts (all take `--project-root`, an id flag, `--output` for exports / a path for validators): `.claude/skills/mmo-quest-creator/scripts/{export,validate}_quest_json.py` (`--quest-id`), `.claude/skills/mmo-item-designer/scripts/{export,validate}_item_json.py` (`--item-id`), `.claude/skills/mmo-npc-designer/scripts/{export,validate}_npc_json.py` (`--unit-id`), `.claude/skills/mmo-spell-designer/scripts/{export,validate}_spell_json.py` (`--spell-id`); `tools/xp_audit.py` (exit 1 when coverage < 120%); `proto_runtime.load_modules` for id enumeration.
- Produces: `tools/gate/reports/content-audit-YYYY-MM-DD.json`; exit 0 iff everything passed. Registered weekly by Task 6's `register_scheduled_tasks.ps1`.

- [ ] **Step 1: Write the audit script**

Create `tools/gate/content_audit.py`:

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Weekly content integrity audit.

Round-trips every quest, item, NPC and spell through its authoring skill's
export + validate scripts (catching broken references and invalid data), then
runs the XP coverage audit (tools/xp_audit.py, threshold 120%). Writes a JSON
report to tools/gate/reports/ and exits non-zero if anything failed.

Runs each check as a subprocess for isolation; a full run takes a while and is
meant for the weekly scheduled task. Use --limit for a quick smoke test.

Usage:
    python tools/gate/content_audit.py                 # everything
    python tools/gate/content_audit.py --domain quest  # one domain
    python tools/gate/content_audit.py --limit 3       # smoke test
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SKILLS = REPO / ".claude" / "skills"
DATA = REPO / "data" / "editor" / "data"

# domain -> (skill dir, export script, validate script, id flag, module key, message class, data file)
DOMAINS = {
    "quest": ("mmo-quest-creator", "export_quest_json.py", "validate_quest_json.py", "--quest-id", "quests", "Quests", "quests.data"),
    "item": ("mmo-item-designer", "export_item_json.py", "validate_item_json.py", "--item-id", "items", "Items", "items.data"),
    "npc": ("mmo-npc-designer", "export_npc_json.py", "validate_npc_json.py", "--unit-id", "units", "Units", "units.data"),
    "spell": ("mmo-spell-designer", "export_spell_json.py", "validate_spell_json.py", "--spell-id", "spells", "Spells", "spells.data"),
}

_mods = None


def proto_modules():
    global _mods
    if _mods is None:
        sys.path.insert(0, str(SKILLS / "mmo-quest-creator" / "scripts"))
        from proto_runtime import load_modules
        _mods = load_modules(REPO)
    return _mods


def entity_ids(module_key: str, message_name: str, data_file: str) -> list[int]:
    msg = getattr(proto_modules()[module_key], message_name)()
    msg.ParseFromString((DATA / data_file).read_bytes())
    return sorted(entry.id for entry in msg.entry)


def run_script(script: Path, args: list[str]) -> tuple[int, str]:
    proc = subprocess.run(
        [sys.executable, str(script), "--project-root", str(REPO), *args],
        capture_output=True, text=True, cwd=str(script.parent))
    return proc.returncode, (proc.stdout + proc.stderr).strip()


def audit_domain(name: str, limit: int | None, tmp_dir: Path) -> dict:
    skill, export_script, validate_script, id_flag, module_key, message_name, data_file = DOMAINS[name]
    scripts = SKILLS / skill / "scripts"
    ids = entity_ids(module_key, message_name, data_file)
    if limit:
        ids = ids[:limit]

    failures = []
    for entity_id in ids:
        draft = tmp_dir / f"{name}_{entity_id}.json"
        code, output = run_script(scripts / export_script, [id_flag, str(entity_id), "--output", str(draft)])
        if code != 0:
            failures.append({"id": entity_id, "stage": "export", "output": output[-2000:]})
            continue
        code, output = run_script(scripts / validate_script, [str(draft)])
        if code != 0:
            failures.append({"id": entity_id, "stage": "validate", "output": output[-2000:]})

    print(f"[{name}] checked {len(ids)}, failures {len(failures)}")
    return {"checked": len(ids), "failures": failures}


def audit_xp() -> dict:
    code, output = run_script(REPO / "tools" / "xp_audit.py", [])
    print(f"[xp] {'passed' if code == 0 else 'FAILED'}")
    return {"passed": code == 0, "output_tail": output[-2000:]}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--domain", choices=[*DOMAINS, "xp"], action="append",
                        help="restrict to specific domain(s); default: all four + xp")
    parser.add_argument("--limit", type=int, help="max entities per domain (smoke test)")
    args = parser.parse_args()

    selected = args.domain or [*DOMAINS, "xp"]
    report = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "limit": args.limit,
        "domains": {},
        "passed": True,
    }

    with tempfile.TemporaryDirectory(prefix="mmo_content_audit_") as tmp:
        tmp_dir = Path(tmp)
        for name in selected:
            result = audit_xp() if name == "xp" else audit_domain(name, args.limit, tmp_dir)
            report["domains"][name] = result
            failed = (not result["passed"]) if name == "xp" else bool(result["failures"])
            if failed:
                report["passed"] = False

    report_dir = REPO / "tools" / "gate" / "reports"
    report_dir.mkdir(parents=True, exist_ok=True)
    path = report_dir / f"content-audit-{datetime.now().strftime('%Y-%m-%d')}.json"
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Report: {path}")
    print("Result:", "GREEN" if report["passed"] else "RED")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Smoke-test with a limit**

Run: `python tools/gate/content_audit.py --limit 2`
Expected: four `[<domain>] checked 2, failures 0` lines (or real failures — see below), one `[xp] passed` line, `Result: GREEN`, and a `tools/gate/reports/content-audit-<today>.json` file. If a domain reports failures, inspect the recorded output: a failure here is either an audit-script bug (fix it) or a REAL data problem (report it to the user; do not "fix" game data in this task).

- [ ] **Step 3: Full run**

Run: `python tools/gate/content_audit.py`
Expected: all domains checked at full count, `Result: GREEN` (several minutes — each entity is an export + validate subprocess pair). Same triage rule as Step 2 if anything is red.

- [ ] **Step 4: Commit**

```bash
git add tools/gate/content_audit.py
git commit -m "Add weekly content integrity audit (round-trip validators + XP coverage)"
```

---

### Task 8: Rollout verification (green and red paths)

**Files:** none created (temporary branches only).

**Interfaces:**
- Consumes: everything from Tasks 1–5.

- [ ] **Step 1: Green path — gated feature branch ships**

```bash
git checkout -b feature/gate-rollout-test
```

Make a trivial change: append a line `- The gate flow was verified end-to-end on 2026-07-24.` to `docs/superpowers/specs/2026-07-24-agentic-dev-flow-design.md`, then commit it on the branch.

Run the full gate (no `-SkipE2E` — this is the one full-pipeline proof):
`powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1`
Expected: exit 0, `Gate result: GREEN`, report `passed: true`, `e2e_skipped: false`, `commit` == branch HEAD.

Then execute the `/ship` procedure exactly as written in `.claude/commands/ship.md`: verify all five preconditions pass, merge `--no-ff` into develop, delete the branch.
Expected: `git log --oneline -1 develop` shows the merge commit `Merge feature/gate-rollout-test (gate green at <sha8>)`.

- [ ] **Step 2: Red path — broken test is blocked**

```bash
git checkout -b feature/gate-red-test
```

Break a unit test deliberately: in `src/unit_tests/test_base64.cpp`, change one existing assertion to expect a wrong value (e.g. append `_BROKEN` to an expected string literal), commit on the branch.

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -SkipE2E`
Expected: exit 1, `Gate result: RED`, report `passed: false` with the `unit_tests` step failed and later steps absent.

Then check the `/ship` preconditions from `.claude/commands/ship.md` against this state.
Expected: refusal — precondition 4 fails (`passed` is false; `e2e_skipped` true would also block). Confirm no merge happened: `git log --oneline -1 develop` is unchanged.

Clean up:

```bash
git checkout develop
git branch -D feature/gate-red-test
```

- [ ] **Step 3: Confirm develop is clean and report**

Run: `git status` (clean, on develop) and `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -SkipE2E` one final time on develop.
Expected: GREEN — proving the red-path branch left no residue. Summarize both drills for the user: green path shipped, red path blocked.
