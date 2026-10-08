# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Registers the recurring maintenance jobs in Windows Task Scheduler (current user),
# via the ScheduledTasks cmdlets (not schtasks). Idempotent: -Force overwrites existing
# definitions. Re-run after changing schedules.
#
# StartWhenAvailable is set so a missed run (machine asleep/off at the scheduled time) fires
# as soon as the machine is next available, instead of silently never running until the next
# scheduled slot.
#
# The nightly full gate runs in GitHub Actions (.github/workflows/nightly-release.yml), not
# here; an "MMO Nightly Gate" task left over from before is removed.

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
. (Join-Path $PSScriptRoot "gate_worktree.ps1")
# Always the main checkout's reports, even when this runs from a feature worktree.
$reportDir = Get-ReportDir
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null

if (Get-ScheduledTask -TaskName "MMO Nightly Gate" -ErrorAction SilentlyContinue)
{
	Unregister-ScheduledTask -TaskName "MMO Nightly Gate" -Confirm:$false
}

$contentAuditScript = Join-Path $repoRoot "tools\gate\content_audit.py"
$contentAuditLog = Join-Path $reportDir "content-audit-task.log"
$contentAuditCommand = "& python '{0}' *>> '{1}'" -f $contentAuditScript, $contentAuditLog

$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable

$contentAuditAction = New-ScheduledTaskAction -Execute "powershell" -Argument ("-NoProfile -ExecutionPolicy Bypass -Command `"{0}`"" -f $contentAuditCommand)
$contentAuditTrigger = New-ScheduledTaskTrigger -Weekly -DaysOfWeek Sunday -At "04:00"
Register-ScheduledTask -TaskName "MMO Weekly Content Audit" -Action $contentAuditAction -Trigger $contentAuditTrigger -Settings $settings -Force | Out-Null

Write-Host "Scheduled tasks registered. Verify with: Get-ScheduledTask -TaskName `"MMO Weekly Content Audit`""
