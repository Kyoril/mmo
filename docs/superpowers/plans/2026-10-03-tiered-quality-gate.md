# Tiered Quality Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make merges to develop cost ~1.5 min (fast gate) while the full gate (E2E) runs nightly in a dedicated worktree that actually executes, and becomes mandatory before a release.

**Architecture:** `verify.ps1` gains a `-Tier fast|full` switch. A dot-sourced helper library `tools/gate/gate_worktree.ps1` owns the dedicated `H:/mmo-nightly` worktree (create, move to a commit, sync submodules, configure) and report lookup. `nightly_gate.ps1` and the new `release_check.ps1` are thin scripts on top of it. Slash-command docs (`/gate`, `/ship`, new `/release`) and CLAUDE.md describe the tiers.

**Tech Stack:** Windows PowerShell 5.1, git worktrees, CMake (VS 2026 generator), Python 3 stdlib `unittest` for the helper tests, Windows Task Scheduler.

**Spec:** [docs/superpowers/specs/2026-10-03-tiered-quality-gate-design.md](../specs/2026-10-03-tiered-quality-gate-design.md)

## Global Constraints

- Every new source file starts with `# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- PowerShell: Allman braces, braces on every `if`, tabs for indentation, must run on Windows PowerShell 5.1 (`powershell`, not `pwsh`).
- Native commands under `$ErrorActionPreference = "Stop"` must be wrapped with a local `"Continue"` when their stderr is redirected (PS 5.1 turns redirected stderr into terminating errors) — pass/fail rests on `$LASTEXITCODE`.
- Reports are UTF-8 with BOM (`Set-Content -Encoding UTF8`); Python readers use `encoding="utf-8-sig"`.
- `tools/gate/reports/`, `tools/gate/logs/`, `tools/gate/last_report.json` are gitignored — never commit them.
- Never push. Never use `--force` on branches or rewrite history. `git checkout --force` is allowed **only** inside the dedicated nightly worktree.
- The nightly worktree is `H:/mmo-nightly` (sibling of the main checkout, overridable with `$env:MMO_NIGHTLY_WORKTREE`). No session works in it.
- Do not put the MySQL password into any tracked file. Sessions set `$env:MMO_E2E_MYSQL_PASSWORD` from memory/env, the scheduled task inherits the persistent user env var.

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `tools/gate/verify.ps1` | Modify | `-Tier fast\|full`, `tier` report field, `$env:MMO_GATE_PYTHON` override |
| `tools/gate/gate_worktree.ps1` | Create | Shared helpers: repo/worktree/report paths, git wrapper, worktree init + configure, lock, report lookup |
| `tools/tests/test_gate_worktree.py` | Create | Tests for report selection helpers (runs in the gate's `tool_tests` step) |
| `tools/gate/release_check.ps1` | Create | Green full report for a commit → exit 0, else run full gate in nightly worktree |
| `.claude/commands/release.md` | Create | `/release` command |
| `tools/gate/nightly_gate.ps1` | Rewrite | Full gate in nightly worktree, unchanged-commit short circuit, suspects list |
| `tools/gate/register_scheduled_tasks.ps1` | Modify | Create nightly worktree, task moves it to develop then runs develop's copy of the script |
| `.claude/commands/gate.md` | Rewrite | Fast by default, `/gate full` for E2E + review |
| `.claude/commands/ship.md` | Rewrite | Runs fast gate itself, worktree-aware merge |
| `CLAUDE.md` | Modify | Agentic Workflow section |

---

### Task 1: Worktree setup and `verify.ps1` tiers

The feature branch lives in `H:/mmo/.claude/worktrees/fast-gate` (branch `feature/fast-gate`, already created). It has no submodules and no `build/` yet; both are needed to run the gate here.

**Files:**
- Modify: `tools/gate/verify.ps1`

**Interfaces:**
- Produces: `verify.ps1 -Tier fast|full` (default `full`; `-SkipE2E` still accepted and means `fast`). `last_report.json` gains `"tier": "fast"|"full"`; `e2e_skipped` stays and equals `tier -ne "full"`. Python executable = `$env:MMO_GATE_PYTHON` if set, else `python`.

- [ ] **Step 1: Initialize submodules and configure the worktree build**

All commands from `H:/mmo/.claude/worktrees/fast-gate` (PowerShell tool):

```powershell
git -c protocol.file.allow=always submodule update --init
cmake -S . -B build -G "Visual Studio 18 2026" -DMMO_BUILD_BOTS=ON -DMMO_BUILD_CLIENT=ON -DMMO_BUILD_EDITOR=ON -DMMO_BUILD_LAUNCHER=ON -DMMO_BUILD_TESTS=ON -DMMO_BUILD_TOOLS=ON -DMMO_DISABLE_ITERATOR_DEBUG=ON -DMMO_ENABLE_ASAN=OFF -DMMO_OPENSSL_STATIC=ON -DMMO_UNITY_BUILD=OFF -DMMO_WITH_DEV_COMMANDS=ON -DMMO_WITH_DISCORD_RPC=OFF -DMMO_WITH_RELEASE_ASSERTS=OFF
```

Expected: submodule update lists `data/client`, `data/editor` and `deps/*` checkouts; configure ends with `-- Generating done` and `-- Build files have been written to: .../fast-gate/build`.

- [ ] **Step 2: Add the `-Tier` parameter and python override**

In `tools/gate/verify.ps1` replace the header usage block and `param(...)`:

```powershell
# Local quality gate: protocol version check -> build -> unit tests -> E2E. Writes tools/gate/last_report.json
# and exits 0 only if every step passed.
#
# Tiers:
#   fast  protocol check, tool tests, build, unit tests (~1.5 min incremental). What /ship requires.
#   full  fast + E2E (~8.5 min). Run nightly in H:/mmo-nightly, by /gate full and by release_check.ps1.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -Tier fast
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1            # full
#
# $env:MMO_GATE_PYTHON overrides the python executable (the WindowsApps "python" alias is not
# launchable from every context, e.g. Claude sessions or Task Scheduler).

[CmdletBinding()]
param(
	[ValidateSet("fast", "full")]
	[string]$Tier = "full",
	# Legacy spelling of -Tier fast.
	[switch]$SkipE2E,
	# all_tests is an aggregate that every mmo_add_test() suite adds itself to, so a new
	# test suite never needs an edit here.
	[string[]]$Targets = @("login_server", "realm_server", "world_server", "e2e_client", "all_tests")
)

if ($SkipE2E)
{
	$Tier = "fast"
}

$python = if ($env:MMO_GATE_PYTHON) { $env:MMO_GATE_PYTHON } else { "python" }
```

(The `$python = if ...` one-liner is an expression, not an `if` block; keep it on one line.)

- [ ] **Step 3: Use `$python` and `$Tier` in the body**

Replace the three `-Exe "python"` occurrences (steps `protocol`, `protocol_tests`, `tool_tests`) with `-Exe $python`.

Replace

```powershell
	if ($ok -and -not $SkipE2E)
```

with

```powershell
	if ($ok -and $Tier -eq "full")
```

Replace the report block's

```powershell
		e2e_skipped = [bool]$SkipE2E
```

with

```powershell
		tier = $Tier
		e2e_skipped = ($Tier -ne "full")
```

- [ ] **Step 4: Run the fast tier**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -Tier fast
```

Expected: steps `protocol`, `protocol_tests`, `tool_tests`, `build`, `tests` run, no `== e2e ==`, last line `Gate result: GREEN`. First build in this worktree is from scratch (~3 min). If `protocol` fails with "cannot execute" / "Das System kann die angegebene Datei nicht finden", set `$env:MMO_GATE_PYTHON` to a python with `protobuf numpy Pillow` (see memory `agentic-dev-flow-gate`, Python314 venv) and re-run.

Then:

```powershell
Get-Content -Raw tools/gate/last_report.json | ConvertFrom-Json | Select-Object tier, e2e_skipped, passed, commit
```

Expected: `tier = fast`, `e2e_skipped = True`, `passed = True`, `commit` = `git rev-parse HEAD`.

- [ ] **Step 5: Commit**

```powershell
git add tools/gate/verify.ps1
git commit -m "feat(gate): fast/full tiers in verify.ps1 and MMO_GATE_PYTHON override`n`nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: `gate_worktree.ps1` helper library

**Files:**
- Create: `tools/gate/gate_worktree.ps1`
- Test: `tools/tests/test_gate_worktree.py`

**Interfaces:**
- Consumes: report fields `commit`, `passed`, `tier`, `e2e_skipped` from Task 1.
- Produces (dot-source with `. (Join-Path $PSScriptRoot "gate_worktree.ps1")`):
  - `Get-MainRepoRoot` → `[string]` main checkout root (e.g. `H:\mmo`), from any worktree.
  - `Get-NightlyWorktreePath` → `[string]` `$env:MMO_NIGHTLY_WORKTREE` or `<parent of main>\mmo-nightly`.
  - `Get-ReportDir` → `[string]` `$env:MMO_GATE_REPORT_DIR` or `<main>\tools\gate\reports`.
  - `Invoke-Git [args...]` → output to host; throws on non-zero exit.
  - `Initialize-GateWorktree -Commit <sha>` → `[string]` worktree path; throws on failure.
  - `Invoke-WorktreeGate -Worktree <path>` → `[int]` exit code of a full-tier `verify.ps1` run there.
  - `Enter-GateWorktreeLock` → `[System.Threading.Mutex]` (held); release with `Exit-GateWorktreeLock $mutex`.
  - `Read-GateReport -Path <file>` → parsed object or `$null`.
  - `Test-GreenFullReport -Report <obj>` → `[bool]`.
  - `Find-GreenFullReport -Commit <sha>` → `[string]` path or `$null`.
  - `Get-LastGreenNightly` → newest green full `nightly-*.json` report object (with `.Path` note property) or `$null`.

- [ ] **Step 1: Write the failing test**

Create `tools/tests/test_gate_worktree.py`:

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the report-selection helpers in tools/gate/gate_worktree.ps1.

/ship, /release and the nightly gate decide "is this commit covered?" through these
helpers. A wrong answer either blocks a release for nothing or, worse, waves through a
commit whose E2E never ran -- so the edge cases (fast-tier reports, red reports, legacy
reports written before the tier field existed) are pinned down here.

Windows only: the helpers are Windows PowerShell 5.1 code.

    python tools/tests/test_gate_worktree.py
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

HELPERS = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "gate", "gate_worktree.ps1"))


def same_path(a, b):
	return os.path.normcase(os.path.realpath(a)) == os.path.normcase(os.path.realpath(b))


def write_report(directory, name, **fields):
	path = os.path.join(directory, name)
	with open(path, "w", encoding="utf-8-sig") as f:
		json.dump(fields, f)
	return path


@unittest.skipUnless(sys.platform == "win32", "gate helpers are Windows PowerShell")
class GateWorktreeTests(unittest.TestCase):
	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.reports = os.path.join(self._tmp.name, "reports")
		self.nightly = os.path.join(self._tmp.name, "nightly")
		os.makedirs(self.reports)
		os.makedirs(os.path.join(self.nightly, "tools", "gate"))

	def tearDown(self):
		self._tmp.cleanup()

	def ps(self, script):
		env = dict(os.environ, MMO_GATE_REPORT_DIR=self.reports, MMO_NIGHTLY_WORKTREE=self.nightly)
		result = subprocess.run(
			["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", ". '{}'; {}".format(HELPERS, script)],
			capture_output=True, text=True, env=env)
		self.assertEqual(result.returncode, 0, result.stderr)
		return result.stdout.strip()

	def green(self, path):
		return self.ps("Test-GreenFullReport -Report (Read-GateReport -Path '{}')".format(path))

	def test_full_green_report_counts(self):
		path = write_report(self.reports, "a.json", commit="c1", passed=True, tier="full", e2e_skipped=False)
		self.assertEqual(self.green(path), "True")

	def test_fast_report_does_not_count(self):
		path = write_report(self.reports, "a.json", commit="c1", passed=True, tier="fast", e2e_skipped=True)
		self.assertEqual(self.green(path), "False")

	def test_red_full_report_does_not_count(self):
		path = write_report(self.reports, "a.json", commit="c1", passed=False, tier="full", e2e_skipped=False)
		self.assertEqual(self.green(path), "False")

	def test_legacy_report_without_tier_counts_when_e2e_ran(self):
		path = write_report(self.reports, "a.json", commit="c1", passed=True, e2e_skipped=False)
		self.assertEqual(self.green(path), "True")

	def test_skipped_nightly_does_not_count(self):
		path = write_report(self.reports, "a.json", skipped=True, passed=None)
		self.assertEqual(self.green(path), "False")

	def test_find_matches_commit_in_report_dir(self):
		write_report(self.reports, "nightly-2026-10-01.json", commit="other", passed=True, tier="full")
		expected = write_report(self.reports, "release-c1.json", commit="c1", passed=True, tier="full")
		self.assertTrue(same_path(self.ps("Find-GreenFullReport -Commit 'c1'"), expected))

	def test_find_checks_nightly_worktree_last_report(self):
		expected = write_report(os.path.join(self.nightly, "tools", "gate"), "last_report.json",
			commit="c2", passed=True, tier="full")
		self.assertTrue(same_path(self.ps("Find-GreenFullReport -Commit 'c2'"), expected))

	def test_find_returns_nothing_for_uncovered_commit(self):
		write_report(self.reports, "nightly-2026-10-01.json", commit="c1", passed=True, tier="fast")
		self.assertEqual(self.ps("Find-GreenFullReport -Commit 'c1'"), "")

	def test_last_green_nightly_skips_red_and_skipped_runs(self):
		write_report(self.reports, "nightly-2026-10-01.json", commit="old", passed=True, tier="full")
		write_report(self.reports, "nightly-2026-10-02.json", commit="good", passed=True, tier="full")
		write_report(self.reports, "nightly-2026-10-03.json", commit="bad", passed=False, tier="full")
		write_report(self.reports, "nightly-2026-10-04.json", skipped=True, passed=None)
		write_report(self.reports, "release-zzz.json", commit="rel", passed=True, tier="full")
		self.assertEqual(self.ps("(Get-LastGreenNightly).commit"), "good")

	def test_last_green_nightly_none(self):
		write_report(self.reports, "nightly-2026-10-03.json", commit="bad", passed=False, tier="full")
		self.assertEqual(self.ps("$null -eq (Get-LastGreenNightly)"), "True")


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python tools/tests/test_gate_worktree.py -v` (or `$env:MMO_GATE_PYTHON`)
Expected: every test FAILS/ERRORs — PowerShell reports the helper file is missing (`. : The term '...gate_worktree.ps1' is not recognized`).

- [ ] **Step 3: Write the helper library**

Create `tools/gate/gate_worktree.ps1`:

```powershell
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Shared helpers for gate runs in the dedicated nightly worktree (H:/mmo-nightly by
# default) and for finding gate reports. Dot-source it:
#   . (Join-Path $PSScriptRoot "gate_worktree.ps1")
# Used by nightly_gate.ps1, release_check.ps1 and register_scheduled_tasks.ps1.
# No session works in the nightly worktree: it is force-checked-out on every run.
#
# Overrides (tests): $env:MMO_NIGHTLY_WORKTREE, $env:MMO_GATE_REPORT_DIR.

function Get-MainRepoRoot
{
	# The common git dir is the main checkout's .git, whichever worktree this runs from.
	$common = (& git -C $PSScriptRoot rev-parse --path-format=absolute --git-common-dir)
	return (Split-Path -Parent $common)
}

function Get-NightlyWorktreePath
{
	if ($env:MMO_NIGHTLY_WORKTREE)
	{
		return $env:MMO_NIGHTLY_WORKTREE
	}
	return (Join-Path (Split-Path -Parent (Get-MainRepoRoot)) "mmo-nightly")
}

function Get-ReportDir
{
	if ($env:MMO_GATE_REPORT_DIR)
	{
		return $env:MMO_GATE_REPORT_DIR
	}
	return (Join-Path (Get-MainRepoRoot) "tools\gate\reports")
}

function Invoke-Git
{
	# PS 5.1 turns redirected native stderr into terminating errors under "Stop"; git writes
	# progress to stderr, so the verdict rests on the exit code alone.
	$ErrorActionPreference = "Continue"
	& git @args 2>&1 | Out-Host
	if ($LASTEXITCODE -ne 0)
	{
		throw ("git {0} failed with exit code {1}" -f ($args -join " "), $LASTEXITCODE)
	}
}

function Invoke-GateConfigure
{
	param(
		[Parameter(Mandatory)][string]$Source,
		[Parameter(Mandatory)][string]$MainBuild
	)

	# Mirror the main checkout's generator and MMO_* options, so the gate builds what the
	# developer builds.
	$cache = Get-Content -Path (Join-Path $MainBuild "CMakeCache.txt")
	$generator = ($cache | Select-String -Pattern '^CMAKE_GENERATOR:INTERNAL=(.*)$').Matches[0].Groups[1].Value
	$options = @($cache | Select-String -Pattern '^(MMO_[A-Z0-9_]+):(BOOL|STRING)=(.*)$' | ForEach-Object { "-D{0}={1}" -f $_.Matches[0].Groups[1].Value, $_.Matches[0].Groups[3].Value })

	$cmakeArgs = @("-S", $Source, "-B", (Join-Path $Source "build"), "-G", $generator) + $options
	$ErrorActionPreference = "Continue"
	& cmake @cmakeArgs 2>&1 | Out-Host
	if ($LASTEXITCODE -ne 0)
	{
		throw ("cmake configure of {0} failed with exit code {1}" -f $Source, $LASTEXITCODE)
	}
}

function Initialize-GateWorktree
{
	param(
		[Parameter(Mandatory)][string]$Commit
	)

	$main = Get-MainRepoRoot
	$worktree = Get-NightlyWorktreePath

	if (-not (Test-Path (Join-Path $worktree ".git")))
	{
		Invoke-Git -C $main worktree add --detach $worktree $Commit
	}

	# --force: the worktree is the gate's alone; anything left behind is a previous run's debris.
	Invoke-Git -C $worktree checkout --detach --force $Commit
	# Submodule URLs are local paths, so file transport must be allowed. A pointer to a
	# submodule commit that exists only in some other worktree fails here -- that is a real
	# defect (develop references an unfetchable commit), and surfaces as a setup error.
	Invoke-Git -C $worktree -c protocol.file.allow=always submodule update --init --force

	if (-not (Test-Path (Join-Path $worktree "build\CMakeCache.txt")))
	{
		Invoke-GateConfigure -Source $worktree -MainBuild (Join-Path $main "build")
	}

	return $worktree
}

function Invoke-WorktreeGate
{
	param(
		[Parameter(Mandatory)][string]$Worktree
	)

	& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Worktree "tools\gate\verify.ps1") -Tier full | Out-Host
	return $LASTEXITCODE
}

function Enter-GateWorktreeLock
{
	# Nightly and release runs share one worktree, one build dir and the E2E ports; a second
	# caller waits instead of trampling the first. Local\ is enough: the scheduled task runs
	# in the logged-on user's session.
	$mutex = New-Object System.Threading.Mutex($false, "Local\MMOGateWorktree")
	try
	{
		$null = $mutex.WaitOne()
	}
	catch [System.Threading.AbandonedMutexException]
	{
		# The previous holder died; we own it now.
	}
	return $mutex
}

function Exit-GateWorktreeLock
{
	param(
		[Parameter(Mandatory)][System.Threading.Mutex]$Mutex
	)

	$Mutex.ReleaseMutex()
	$Mutex.Dispose()
}

function Read-GateReport
{
	param(
		[Parameter(Mandatory)][string]$Path
	)

	if (-not (Test-Path $Path))
	{
		return $null
	}
	try
	{
		return (Get-Content -Raw -Path $Path | ConvertFrom-Json)
	}
	catch
	{
		return $null
	}
}

function Test-GreenFullReport
{
	param(
		$Report
	)

	if ($null -eq $Report -or $Report.passed -ne $true)
	{
		return $false
	}
	if ($Report.tier)
	{
		return ($Report.tier -eq "full")
	}
	# Reports written before the tier field existed: full means E2E was not skipped.
	return ($Report.e2e_skipped -eq $false)
}

function Find-GreenFullReport
{
	param(
		[Parameter(Mandatory)][string]$Commit
	)

	$candidates = @()
	$reportDir = Get-ReportDir
	if (Test-Path $reportDir)
	{
		$candidates += @(Get-ChildItem -Path $reportDir -Filter "*.json" | Sort-Object LastWriteTime -Descending | ForEach-Object { $_.FullName })
	}
	$candidates += (Join-Path (Get-NightlyWorktreePath) "tools\gate\last_report.json")

	foreach ($path in $candidates)
	{
		$report = Read-GateReport -Path $path
		if ($report -and $report.commit -eq $Commit -and (Test-GreenFullReport -Report $report))
		{
			return $path
		}
	}
	return $null
}

function Get-LastGreenNightly
{
	$reportDir = Get-ReportDir
	if (-not (Test-Path $reportDir))
	{
		return $null
	}

	# File names sort by date (nightly-YYYY-MM-DD.json).
	foreach ($file in (Get-ChildItem -Path $reportDir -Filter "nightly-*.json" | Sort-Object Name -Descending))
	{
		$report = Read-GateReport -Path $file.FullName
		if (Test-GreenFullReport -Report $report)
		{
			$report | Add-Member -NotePropertyName Path -NotePropertyValue $file.FullName -Force
			return $report
		}
	}
	return $null
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `python tools/tests/test_gate_worktree.py -v`
Expected: 10 tests, `OK`.

- [ ] **Step 5: Smoke-check path helpers against the real repo**

```powershell
powershell -NoProfile -Command ". .\tools\gate\gate_worktree.ps1; Get-MainRepoRoot; Get-NightlyWorktreePath; Get-ReportDir"
```

Expected: `H:\mmo` (or `H:/mmo`), `H:\mmo-nightly`, `H:\mmo\tools\gate\reports`.

- [ ] **Step 6: Commit**

```powershell
git add tools/gate/gate_worktree.ps1 tools/tests/test_gate_worktree.py
git commit -m "feat(gate): shared nightly-worktree and report-lookup helpers`n`nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `release_check.ps1` and `/release`

**Files:**
- Create: `tools/gate/release_check.ps1`
- Create: `.claude/commands/release.md`

**Interfaces:**
- Consumes: everything from Task 2; `verify.ps1 -Tier full` from Task 1.
- Produces: `release_check.ps1 [-Ref <commit-ish>] [-NoRun]`, exit 0 = safe to publish, 1 = not green, 2 = unknown ref. Writes `tools/gate/reports/release-<sha8>.json` (main checkout) when it runs the gate. A future publish script calls this first.

- [ ] **Step 1: Write `release_check.ps1`**

```powershell
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Release tier of the quality gate: a commit may be published to live only with a green
# FULL gate (build + unit tests + E2E) for that exact commit. Exit 0 = safe to publish.
#
# If no nightly or earlier release check covered the commit, runs the full gate on it in
# the dedicated nightly worktree (~10 min) unless -NoRun is given.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/release_check.ps1 [-Ref develop] [-NoRun]
#
# Exit codes: 0 green, 1 not green (or setup failure), 2 unknown ref.

[CmdletBinding()]
param(
	[string]$Ref = "develop",
	[switch]$NoRun
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "gate_worktree.ps1")

$main = Get-MainRepoRoot
$commit = (& git -C $main rev-parse --verify --quiet ("{0}^{{commit}}" -f $Ref))
if ($LASTEXITCODE -ne 0 -or -not $commit)
{
	Write-Host ("Unknown ref '{0}'." -f $Ref) -ForegroundColor Red
	exit 2
}
$short = $commit.Substring(0, 8)

$found = Find-GreenFullReport -Commit $commit
if ($found)
{
	Write-Host ("GREEN: {0} ({1}) has a green full gate: {2}" -f $short, $Ref, $found) -ForegroundColor Green
	Write-Host ("Safe to publish {0}." -f $short)
	exit 0
}

if ($NoRun)
{
	Write-Host ("RED: no green full gate report for {0} ({1}). Run without -NoRun to gate it." -f $short, $Ref) -ForegroundColor Red
	exit 1
}

$reportDir = Get-ReportDir
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
$target = Join-Path $reportDir ("release-{0}.json" -f $short)

Write-Host ("No full gate covers {0} yet - running it in the nightly worktree." -f $short)
$lock = Enter-GateWorktreeLock
try
{
	try
	{
		$worktree = Initialize-GateWorktree -Commit $commit
	}
	catch
	{
		Write-Host ("RED: could not prepare the nightly worktree: {0}" -f $_.Exception.Message) -ForegroundColor Red
		exit 1
	}

	$exit = Invoke-WorktreeGate -Worktree $worktree
	$source = Join-Path $worktree "tools\gate\last_report.json"
	if (Test-Path $source)
	{
		Copy-Item -Path $source -Destination $target -Force
	}

	if ($exit -eq 0)
	{
		Write-Host ("GREEN: full gate passed for {0} ({1}). Report: {2}" -f $short, $Ref, $target) -ForegroundColor Green
		Write-Host ("Safe to publish {0}." -f $short)
		exit 0
	}

	Write-Host ("RED: full gate failed for {0} ({1}). Report: {2}; step logs: {3}" -f $short, $Ref, $target, (Join-Path $worktree "tools\gate\logs")) -ForegroundColor Red
	exit 1
}
finally
{
	Exit-GateWorktreeLock -Mutex $lock
}
```

- [ ] **Step 2: Verify the not-covered path**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/release_check.ps1 -Ref HEAD -NoRun; "exit=$LASTEXITCODE"
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/release_check.ps1 -Ref no-such-ref -NoRun; "exit=$LASTEXITCODE"
```

Expected: `RED: no green full gate report for <sha8> (HEAD)...` + `exit=1`; then `Unknown ref 'no-such-ref'.` + `exit=2`.

- [ ] **Step 3: Verify the run path end to end (creates `H:/mmo-nightly`)**

Commit first (the worktree checks out the commit, not the working tree), then run in the background (~10–15 min: from-scratch build + E2E):

```powershell
git add tools/gate/release_check.ps1
git commit -m "feat(gate): release_check.ps1 requires a green full gate per commit`n`nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
$env:MMO_E2E_MYSQL_PASSWORD = "<from memory e2e-test-harness>"
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/release_check.ps1 -Ref HEAD; "exit=$LASTEXITCODE"
```

Expected: `H:\mmo-nightly` is created detached at HEAD, submodules initialised, configure + build + tests + e2e run, final `GREEN: full gate passed for <sha8>` + `exit=0`, and `H:\mmo\tools\gate\reports\release-<sha8>.json` exists with `"tier": "full"`, `"passed": true`.

If E2E fails, inspect `H:\mmo-nightly\e2e\runtime\logs\` before assuming a code bug (memory `agentic-dev-flow-gate`: missing `data/*` submodule symptoms). Do not continue with a red run.

- [ ] **Step 4: Verify the covered path**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/release_check.ps1 -Ref HEAD -NoRun; "exit=$LASTEXITCODE"
```

Expected: `GREEN: <sha8> (HEAD) has a green full gate: H:\mmo\tools\gate\reports\release-<sha8>.json`, `exit=0`, returns in a second.

- [ ] **Step 5: Write `.claude/commands/release.md`**

```markdown
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
```

- [ ] **Step 6: Commit**

```powershell
git add .claude/commands/release.md
git commit -m "feat(gate): /release command`n`nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Nightly gate in the dedicated worktree

**Files:**
- Rewrite: `tools/gate/nightly_gate.ps1`
- Modify: `tools/gate/register_scheduled_tasks.ps1`

**Interfaces:**
- Consumes: Task 2 helpers.
- Produces: `nightly_gate.ps1 [-Ref develop] [-Force]` writing `<ReportDir>/nightly-YYYY-MM-DD.json` = the gate report plus `last_green_commit`, `merges_since_last_green` (array of `git log --first-parent --oneline` lines), and either `unchanged: true` or `setup_error: <message>` when applicable. Scheduled task "MMO Nightly Gate" runs `H:\mmo-nightly\tools\gate\nightly_gate.ps1` after moving that worktree to develop.

- [ ] **Step 1: Rewrite `nightly_gate.ps1`**

```powershell
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Nightly FULL gate (build + unit tests + E2E) on develop, run in the dedicated nightly
# worktree (H:/mmo-nightly) so it no longer depends on what the main checkout is doing.
# Writes tools/gate/reports/nightly-YYYY-MM-DD.json in the MAIN checkout: the gate report
# plus the last green commit and the merges since then (the suspects when it is red).
#
# Skips the 8-minute run when develop has not moved since the last green night
# ("unchanged": true); -Force runs anyway.

[CmdletBinding()]
param(
	[string]$Ref = "develop",
	[switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "gate_worktree.ps1")

$main = Get-MainRepoRoot
$reportDir = Get-ReportDir
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
$target = Join-Path $reportDir ("nightly-{0}.json" -f (Get-Date -Format "yyyy-MM-dd"))

$commit = (& git -C $main rev-parse --verify ("{0}^{{commit}}" -f $Ref))
$lastGreen = Get-LastGreenNightly
$lastGreenCommit = if ($lastGreen) { $lastGreen.commit } else { $null }

function Get-MergesSince
{
	param([string]$From, [string]$To)

	if ($From)
	{
		return @(& git -C $main log --first-parent --oneline ("{0}..{1}" -f $From, $To))
	}
	# No green night on record: the last ten merges are the best available suspect list.
	return @(& git -C $main log --first-parent --oneline -n 10 $To)
}

function Write-NightlyReport
{
	param($Report)

	$Report | ConvertTo-Json -Depth 5 | Set-Content -Path $target -Encoding UTF8
}

if (-not $Force -and $lastGreenCommit -eq $commit)
{
	Write-NightlyReport ([ordered]@{
		timestamp = (Get-Date -Format "o")
		commit = $commit
		tier = "full"
		passed = $true
		unchanged = $true
		last_green_commit = $lastGreenCommit
		merges_since_last_green = @()
	})
	Write-Host ("Nightly gate: develop unchanged since the last green night ({0}), not re-run." -f $commit.Substring(0, 8))
	exit 0
}

$merges = Get-MergesSince -From $lastGreenCommit -To $commit

$lock = Enter-GateWorktreeLock
try
{
	try
	{
		$worktree = Initialize-GateWorktree -Commit $commit
	}
	catch
	{
		Write-NightlyReport ([ordered]@{
			timestamp = (Get-Date -Format "o")
			commit = $commit
			tier = "full"
			passed = $false
			setup_error = $_.Exception.Message
			last_green_commit = $lastGreenCommit
			merges_since_last_green = $merges
		})
		Write-Host ("Nightly gate: setup failed: {0}" -f $_.Exception.Message)
		exit 1
	}

	$exit = Invoke-WorktreeGate -Worktree $worktree
	$report = Read-GateReport -Path (Join-Path $worktree "tools\gate\last_report.json")
	if ($null -eq $report -or $report.commit -ne $commit)
	{
		# verify.ps1 died before writing its report (or left a stale one): never let that read as green.
		$report = [pscustomobject][ordered]@{
			timestamp = (Get-Date -Format "o")
			commit = $commit
			tier = "full"
			passed = $false
			setup_error = ("verify.ps1 exited {0} without writing a report for this commit" -f $exit)
		}
	}
	$report | Add-Member -NotePropertyName last_green_commit -NotePropertyValue $lastGreenCommit -Force
	$report | Add-Member -NotePropertyName merges_since_last_green -NotePropertyValue $merges -Force
	Write-NightlyReport $report
	exit $exit
}
finally
{
	Exit-GateWorktreeLock -Mutex $lock
}
```

- [ ] **Step 2: Verify the unchanged short-circuit against a scratch report dir**

Uses the green `release-<sha8>.json` from Task 3 as a fake previous green night (`<sha8>` = the commit gated there):

```powershell
$scratch = Join-Path $env:TEMP "nightly-test"
Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue
New-Item -ItemType Directory $scratch | Out-Null
$release = Get-ChildItem H:\mmo\tools\gate\reports\release-*.json | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Copy-Item $release.FullName (Join-Path $scratch "nightly-2026-01-01.json")
$gated = (Get-Content -Raw $release.FullName | ConvertFrom-Json).commit
$env:MMO_GATE_REPORT_DIR = $scratch
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/nightly_gate.ps1 -Ref $gated; "exit=$LASTEXITCODE"
Get-ChildItem $scratch
Remove-Item Env:\MMO_GATE_REPORT_DIR
```

Expected: `Nightly gate: develop unchanged since the last green night (<sha8>), not re-run.`, `exit=0`, and a `nightly-<today>.json` in the scratch dir with `"unchanged": true`. The real `H:\mmo\tools\gate\reports` gets no new nightly file.

- [ ] **Step 3: Verify the suspects list**

```powershell
powershell -NoProfile -Command ". .\tools\gate\gate_worktree.ps1; & git -C (Get-MainRepoRoot) log --first-parent --oneline HEAD~3..HEAD"
```

Expected: three oneline entries — the same command shape `Get-MergesSince` uses. (The full red/green path is exercised by the real scheduled run in Task 6.)

- [ ] **Step 4: Point the scheduled task at the nightly worktree**

In `tools/gate/register_scheduled_tasks.ps1`, after `New-Item -ItemType Directory -Force -Path $reportDir | Out-Null` add:

```powershell
. (Join-Path $PSScriptRoot "gate_worktree.ps1")

# The nightly gate runs in its own worktree, never in a checkout a session may be using.
$nightlyWorktree = Get-NightlyWorktreePath
if (-not (Test-Path (Join-Path $nightlyWorktree ".git")))
{
	& git -C $repoRoot worktree add --detach $nightlyWorktree develop
	if ($LASTEXITCODE -ne 0)
	{
		throw ("Could not create the nightly worktree at {0}" -f $nightlyWorktree)
	}
}
```

Replace

```powershell
$nightlyScript = Join-Path $repoRoot "tools\gate\nightly_gate.ps1"
$nightlyLog = Join-Path $reportDir "nightly-task.log"
$nightlyCommand = "& '{0}' *>> '{1}'" -f $nightlyScript, $nightlyLog
```

with

```powershell
$nightlyScript = Join-Path $nightlyWorktree "tools\gate\nightly_gate.ps1"
$nightlyLog = Join-Path $reportDir "nightly-task.log"
# Move the worktree to develop before the script starts, so each night runs develop's own
# copy of the gate scripts rather than whatever the previous night left behind.
$nightlyCommand = "& git -C '{0}' checkout --detach --force develop *>> '{1}'; & '{2}' *>> '{1}'" -f $nightlyWorktree, $nightlyLog, $nightlyScript
```

Also update the header comment: replace the line `# Registers the recurring maintenance jobs in Windows Task Scheduler (current user),` paragraph's end with a sentence `# Also creates the dedicated nightly worktree (H:/mmo-nightly) if it does not exist.`

Do NOT run the registration yet — the worktree must point at a develop that contains this script (Task 6).

- [ ] **Step 5: Parse-check both scripts**

```powershell
foreach ($f in "tools/gate/nightly_gate.ps1", "tools/gate/register_scheduled_tasks.ps1") { $e = $null; [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $f), [ref]$null, [ref]$e) | Out-Null; "$f errors: $($e.Count)" }
```

Expected: `errors: 0` for both.

- [ ] **Step 6: Commit**

```powershell
git add tools/gate/nightly_gate.ps1 tools/gate/register_scheduled_tasks.ps1
git commit -m "feat(gate): nightly full gate runs in a dedicated worktree, reports suspects`n`nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: `/gate`, `/ship` and CLAUDE.md

**Files:**
- Rewrite: `.claude/commands/gate.md`
- Rewrite: `.claude/commands/ship.md`
- Modify: `CLAUDE.md` (section `## Agentic Workflow`)

**Interfaces:**
- Consumes: `verify.ps1 -Tier fast|full` (Task 1), `/release` (Task 3), nightly report fields (Task 4).

- [ ] **Step 1: Rewrite `.claude/commands/gate.md`**

```markdown
---
description: Run the local quality gate. Fast tier by default (build + unit tests); "/gate full" adds E2E and a code review.
---

Run the local quality gate for the current branch. Argument: `full` (optional).

## Steps

1. Run the gate:
   - Default: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -Tier fast`
     (protocol check, tool tests, build, unit tests — ~1.5 min incremental).
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
```

- [ ] **Step 2: Rewrite `.claude/commands/ship.md`**

```markdown
---
description: Merge the current feature branch into develop after a green fast gate (runs it if needed). Never pushes.
---

Merge the current feature branch into develop. The merge tier is the fast gate (protocol
check, tool tests, build, unit tests). E2E runs nightly on develop and before releases
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
```

- [ ] **Step 3: Update CLAUDE.md "Agentic Workflow"**

Replace the three bullets starting at `- Merging to \`develop\` goes through the local quality gate:` up to and including the bullet ending `...so parallel worktree sessions run\n  without it.` (but keep the `- Never push to origin...` bullet) with:

```markdown
- The quality gate has three tiers (`tools/gate/verify.ps1 -Tier fast|full`):
  - **Merge (fast):** `/ship` merges a feature branch into `develop` after a green fast
    gate for HEAD — protocol check, tool tests, Debug build, unit tests (~1.5 min). It
    runs the gate itself when needed. `/gate` runs the same check on demand; `/gate full`
    adds E2E and a code review for risky changes.
  - **Nightly (full):** the "MMO Nightly Gate" task runs the full gate including E2E on
    `develop` in the dedicated worktree `H:/mmo-nightly`, which no session may use or
    edit. It skips when develop has not moved since the last green night.
  - **Release (full):** before publishing a build to the live client distribution or
    servers, `/release` (`tools/gate/release_check.ps1`) must be green for that exact
    commit; it runs the full gate if no nightly covered it.
- Never push to origin unless the user explicitly asks.
- Reports land in `tools/gate/reports/` (`nightly-*.json`, `release-*.json`, weekly
  content audit). At session start, surface to the user before starting new work:
  the newest nightly report if `passed` is `false` (quote `merges_since_last_green`, the
  suspects, and `setup_error` if present), or the fact that no nightly report with a
  non-null `passed` is younger than 48 h (the nightly is not running). Reports with
  `"skipped": true` predate the dedicated worktree and mean "did not run".
- `.claude/settings.local.json` (htex MCP config) does not follow git worktrees, so
  parallel worktree sessions run without it.
```

(Keep the `Never push` bullet only once — it is included above; delete the original.)

- [ ] **Step 4: Proofread**

Run: `git diff -- CLAUDE.md .claude/commands`
Expected: no mention of `/ship` requiring E2E or a "full" report anywhere; `grep -n "e2e_skipped\|HEAD-matching" CLAUDE.md .claude/commands/*.md` returns nothing.

- [ ] **Step 5: Commit**

```powershell
git add .claude/commands/gate.md .claude/commands/ship.md CLAUDE.md
git commit -m "docs(gate): fast merge tier, nightly full gate, /release in commands and CLAUDE.md`n`nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Ship, register, prove the nightly runs

**Files:**
- Modify (outside repo): `C:\Users\RobinKlimonow\.claude\projects\H--mmo\memory\agentic-dev-flow-gate.md`, `MEMORY.md` index line

- [ ] **Step 1: Ship with the new flow**

Run `/ship` from `H:/mmo/.claude/worktrees/fast-gate` (it reads the branch's new `ship.md`). Expected: it runs the fast gate (HEAD moved since Task 1), green, merges into whichever checkout holds develop (ask the user if that is not `H:/mmo` or the main checkout is on a feature branch — `H:/mmo` was on `feature/channel-end-notify` when this plan was written, in which case nobody holds develop and the worktree checks it out, merges, detaches).

- [ ] **Step 2: Register the scheduled tasks**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/register_scheduled_tasks.ps1
(Get-ScheduledTask -TaskName "MMO Nightly Gate").Actions.Arguments
```

Expected: `Scheduled tasks registered.`; the arguments contain `git -C 'H:\mmo-nightly' checkout --detach --force develop` and `H:\mmo-nightly\tools\gate\nightly_gate.ps1`. (`H:\mmo-nightly` already exists from Task 3, so no worktree is created.)

- [ ] **Step 3: Trigger one real run under Task Scheduler**

```powershell
Start-ScheduledTask -TaskName "MMO Nightly Gate"
```

Then wait with Monitor (until-loop on `(Get-ScheduledTask -TaskName "MMO Nightly Gate").State -ne "Running"`, ~10–15 min). Expected afterwards: `H:\mmo\tools\gate\reports\nightly-<today>.json` with `"tier": "full"`, `"passed": true`, `last_green_commit` null (first night) and `merges_since_last_green` listing the last ten develop merges. Read `nightly-task.log` tail too.

- [ ] **Step 4: Only if the run failed at `protocol` with a python launch error**

Ask the user before persisting an environment variable, then:

```powershell
& "$env:LOCALAPPDATA\Programs\Python\Python314\python.exe" -m venv "$env:LOCALAPPDATA\mmo-gate-venv"
& "$env:LOCALAPPDATA\mmo-gate-venv\Scripts\python.exe" -m pip install protobuf numpy Pillow
setx MMO_GATE_PYTHON "$env:LOCALAPPDATA\mmo-gate-venv\Scripts\python.exe"
```

Task Scheduler picks the new user env var up on the next start; re-run Step 3.

- [ ] **Step 5: Check the unchanged short-circuit for real**

```powershell
Start-ScheduledTask -TaskName "MMO Nightly Gate"
```

Expected within a minute: today's nightly report is overwritten with `"unchanged": true` (develop did not move). This is the state tomorrow's 03:00 run compares against.

- [ ] **Step 6: Update memory**

Rewrite the top of `agentic-dev-flow-gate.md` (keep the gotchas that still apply: BOM, worktree submodule init, data/editor pointer trap, develop-held-by-any-worktree, python launch, half-started-run poisoning) to describe: three tiers; `/ship` runs fast gate itself; nightly in `H:/mmo-nightly` via task that checks out develop then runs develop's script; `release_check.ps1` exit codes and `release-<sha8>.json`; `MMO_GATE_PYTHON`; `Local\MMOGateWorktree` mutex. Remove the now-false lines: "`-SkipE2E` reports are not shippable", "/ship ... AND a green `e2e` step entry (precondition 6)", "skips with `passed: null` when repo busy". Update the `MEMORY.md` index line hook to mention "tiered (fast merge / nightly full in H:/mmo-nightly / release check)".

- [ ] **Step 7: Report to the user**

Summarise: merge cost now (fast gate duration from the last report), the first nightly report path and verdict, and how to run `/release`.
