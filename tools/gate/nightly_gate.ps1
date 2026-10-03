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

# Script-scope state, so Write-RedReport can describe whatever is known when something fails.
$script:main = $null
$script:target = $null
$script:commit = $null
$script:lastGreenCommit = $null
$script:merges = @()

function Get-MergesSince
{
	param([string]$From, [string]$To)

	if ($From)
	{
		return @(& git -C $script:main log --first-parent --oneline ("{0}..{1}" -f $From, $To))
	}
	# No green night on record: the last ten merges are the best available suspect list.
	return @(& git -C $script:main log --first-parent --oneline -n 10 $To)
}

function Write-NightlyReport
{
	param($Report)

	$Report | ConvertTo-Json -Depth 5 | Set-Content -Path $script:target -Encoding UTF8
}

# A run that cannot start (or dies unexpectedly) must leave a RED report, never green, never nothing.
function Write-RedReport
{
	param([Parameter(Mandatory)][string]$Message)

	Write-NightlyReport ([ordered]@{
		timestamp = (Get-Date -Format "o")
		commit = $script:commit
		tier = "full"
		passed = $false
		setup_error = $Message
		logs_dir = (Join-Path (Get-NightlyWorktreePath) "tools\gate\logs")
		last_green_commit = $script:lastGreenCommit
		merges_since_last_green = @($script:merges)
	})
	Write-Host ("Nightly gate: failed: {0}" -f $Message)
}

$lock = $null
try
{
	$script:main = Get-MainRepoRoot
	$reportDir = Get-ReportDir
	New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
	$script:target = Join-Path $reportDir ("nightly-{0}.json" -f (Get-Date -Format "yyyy-MM-dd"))

	$resolved = @(& git -C $script:main rev-parse --verify --quiet ("{0}^{{commit}}" -f $Ref))
	if ($LASTEXITCODE -ne 0 -or $resolved.Count -eq 0 -or -not $resolved[0])
	{
		Write-RedReport -Message ("cannot resolve ref '{0}'" -f $Ref)
		exit 1
	}
	$script:commit = [string]$resolved[0]

	$lastGreen = Get-LastGreenNightly
	$script:lastGreenCommit = if ($lastGreen) { $lastGreen.commit } else { $null }

	if (-not $Force -and $script:lastGreenCommit -eq $script:commit)
	{
		# Do not clobber today's report if it already is a green full run for this commit.
		$existing = if (Test-Path $script:target) { Read-GateReport -Path $script:target } else { $null }
		if (-not ((Test-GreenFullReport $existing) -and $existing.commit -eq $script:commit))
		{
			Write-NightlyReport ([ordered]@{
				timestamp = (Get-Date -Format "o")
				commit = $script:commit
				tier = "full"
				passed = $true
				unchanged = $true
				last_green_commit = $script:lastGreenCommit
				merges_since_last_green = @()
			})
		}
		Write-Host ("Nightly gate: develop unchanged since the last green night ({0}), not re-run." -f $script:commit.Substring(0, 8))
		exit 0
	}

	$script:merges = @(Get-MergesSince -From $script:lastGreenCommit -To $script:commit)

	$lock = Enter-GateWorktreeLock
	$worktree = Initialize-GateWorktree -Commit $script:commit

	$exit = Invoke-WorktreeGate -Worktree $worktree
	$report = Read-GateReport -Path (Join-Path $worktree "tools\gate\last_report.json")
	if ($null -eq $report -or $report.commit -ne $script:commit)
	{
		# verify.ps1 died before writing its report (or left a stale one): never let that read as green.
		$report = [pscustomobject][ordered]@{
			timestamp = (Get-Date -Format "o")
			commit = $script:commit
			tier = "full"
			passed = $false
			setup_error = ("verify.ps1 exited {0} without writing a report for this commit" -f $exit)
		}
	}
	# A non-zero exit is red whatever the report says (a stale or hand-edited report).
	if ($exit -ne 0)
	{
		$report | Add-Member -NotePropertyName passed -NotePropertyValue $false -Force
	}
	$report | Add-Member -NotePropertyName logs_dir -NotePropertyValue (Join-Path $worktree "tools\gate\logs") -Force
	$report | Add-Member -NotePropertyName last_green_commit -NotePropertyValue $script:lastGreenCommit -Force
	$report | Add-Member -NotePropertyName merges_since_last_green -NotePropertyValue $script:merges -Force
	Write-NightlyReport $report
	exit (if ($report.passed -eq $true) { 0 } else { 1 })
}
catch
{
	Write-RedReport -Message $_.Exception.Message
	exit 1
}
finally
{
	if ($lock)
	{
		Exit-GateWorktreeLock -Mutex $lock
	}
}
