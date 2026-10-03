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
