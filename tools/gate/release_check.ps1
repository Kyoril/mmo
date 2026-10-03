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
$worktree = $null
$initialised = $false
try
{
	try
	{
		$worktree = Initialize-GateWorktree -Commit $commit
		$initialised = $true
	}
	catch
	{
		Write-Host ("RED: could not prepare the nightly worktree: {0}" -f $_.Exception.Message) -ForegroundColor Red
		exit 1
	}

	$exit = Invoke-WorktreeGate -Worktree $worktree
	$logsDir = Join-Path $worktree "tools\gate\logs"
	$report = Read-GateReport -Path (Join-Path $worktree "tools\gate\last_report.json")
	$reportMatches = ($null -ne $report -and $report.commit -eq $commit)
	if ($reportMatches)
	{
		$report | Add-Member -NotePropertyName logs_dir -NotePropertyValue $logsDir -Force
		$report | ConvertTo-Json -Depth 6 | Set-Content -Path $target -Encoding UTF8
	}

	# The exit code alone is not enough: a stale report must not read as green.
	if ($exit -eq 0 -and $reportMatches -and (Test-GreenFullReport -Report $report))
	{
		Write-Host ("GREEN: full gate passed for {0} ({1}). Report: {2}" -f $short, $Ref, $target) -ForegroundColor Green
		Write-Host ("Safe to publish {0}." -f $short)
		exit 0
	}

	Write-Host ("RED: full gate failed for {0} ({1}). Report: {2}; step logs: {3}" -f $short, $Ref, $target, $logsDir) -ForegroundColor Red
	exit 1
}
finally
{
	# The scheduled task runs the worktree's copy of nightly_gate.ps1; leaving it on a commit
	# that predates the tiered gate would make it run an old script forever.
	if ($initialised)
	{
		try
		{
			$developSha = (& git -C $main rev-parse --verify --quiet "develop^{commit}")
			if ($developSha -and $commit -ne $developSha)
			{
				Invoke-Git -C $worktree checkout --detach --force develop
			}
		}
		catch
		{
			Write-Host ("Warning: could not return the nightly worktree to develop: {0}" -f $_.Exception.Message)
		}
	}
	Exit-GateWorktreeLock -Mutex $lock
}