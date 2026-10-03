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
