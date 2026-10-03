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