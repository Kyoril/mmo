# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Registers the recurring maintenance jobs in Windows Task Scheduler (current user),
# via the ScheduledTasks cmdlets (not schtasks). Idempotent: -Force overwrites existing
# definitions. Re-run after changing schedules.
# Also creates the dedicated nightly worktree (H:/mmo-nightly) if it does not exist.
#
# StartWhenAvailable is set on both tasks so a missed run (machine asleep/off at the
# scheduled time) fires as soon as the machine is next available, instead of silently
# never running until the next scheduled slot.
#
# Requires MMO_E2E_MYSQL_PASSWORD as a persistent USER environment variable
# (setx MMO_E2E_MYSQL_PASSWORD "<password>"), otherwise the nightly E2E step fails.

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$reportDir = Join-Path $PSScriptRoot "reports"
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
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

if (-not [Environment]::GetEnvironmentVariable("MMO_E2E_MYSQL_PASSWORD", "User"))
{
	Write-Warning "MMO_E2E_MYSQL_PASSWORD is not set as a user environment variable; the nightly E2E step will fail until it is (setx MMO_E2E_MYSQL_PASSWORD `"<password>`")."
}

$nightlyScript = Join-Path $nightlyWorktree "tools\gate\nightly_gate.ps1"
$nightlyLog = Join-Path $reportDir "nightly-task.log"
# Move the worktree to develop before the script starts, so each night runs develop's own
# copy of the gate scripts rather than whatever the previous night left behind.
$nightlyCommand = "& git -C '{0}' checkout --detach --force develop *>> '{1}'; & '{2}' *>> '{1}'" -f $nightlyWorktree, $nightlyLog, $nightlyScript

$contentAuditScript = Join-Path $repoRoot "tools\gate\content_audit.py"
$contentAuditLog = Join-Path $reportDir "content-audit-task.log"
$contentAuditCommand = "& python '{0}' *>> '{1}'" -f $contentAuditScript, $contentAuditLog

$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable

$nightlyAction = New-ScheduledTaskAction -Execute "powershell" -Argument ("-NoProfile -ExecutionPolicy Bypass -Command `"{0}`"" -f $nightlyCommand)
$nightlyTrigger = New-ScheduledTaskTrigger -Daily -At "03:00"
Register-ScheduledTask -TaskName "MMO Nightly Gate" -Action $nightlyAction -Trigger $nightlyTrigger -Settings $settings -Force | Out-Null

$contentAuditAction = New-ScheduledTaskAction -Execute "powershell" -Argument ("-NoProfile -ExecutionPolicy Bypass -Command `"{0}`"" -f $contentAuditCommand)
$contentAuditTrigger = New-ScheduledTaskTrigger -Weekly -DaysOfWeek Sunday -At "04:00"
Register-ScheduledTask -TaskName "MMO Weekly Content Audit" -Action $contentAuditAction -Trigger $contentAuditTrigger -Settings $settings -Force | Out-Null

Write-Host "Scheduled tasks registered. Verify with: Get-ScheduledTask -TaskName `"MMO Nightly Gate`",`"MMO Weekly Content Audit`""
