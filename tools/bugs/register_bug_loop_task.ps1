# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Registers the "MMO Bug Loop" scheduled task (current user). It starts at logon and runs
# tools/bugs/bug_loop.py watch from a fresh snapshot of origin/develop in H:\mmo-bugloop-runtime,
# never from a checkout a fixer can edit: loop changes take effect only through develop.
# The loop exits with 75 at each UTC day boundary; restart-on-failure brings it back from a new
# snapshot five minutes later.
#
# Requires the user environment variables MMO_BUG_API_KEY and MMO_E2E_MYSQL_PASSWORD, git push
# access to origin (ssh) and to the data submodules' GitHub remotes, and tools/bugs on
# origin/develop.
#
#   powershell -File tools/bugs/register_bug_loop_task.ps1            # live
#   powershell -File tools/bugs/register_bug_loop_task.ps1 -DryRun    # shadow mode

[CmdletBinding()]
param(
	[string]$Runtime = "H:\mmo-bugloop-runtime",
	[switch]$DryRun
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\gate\gate_worktree.ps1")
$main = Get-MainRepoRoot
$reportDir = Get-ReportDir
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
$python = if ($env:MMO_GATE_PYTHON) { $env:MMO_GATE_PYTHON } else { "python" }

foreach ($name in @("MMO_BUG_API_KEY", "MMO_E2E_MYSQL_PASSWORD"))
{
	if (-not [Environment]::GetEnvironmentVariable($name, "User"))
	{
		Write-Warning ("{0} is not set as a user environment variable; the bug loop will fail until it is." -f $name)
	}
}

$mode = if ($DryRun) { " --dry-run" } else { "" }
$log = Join-Path $reportDir "bugloop-task.log"
$archive = Join-Path $env:TEMP "mmo-bugloop-runtime.tar"
# PowerShell 5.1 corrupts binary pipes between native commands, so the archive goes through a file.
$command = ("git -C '{0}' fetch origin --quiet; " +
	"if (Test-Path '{1}') {{ Remove-Item -Recurse -Force '{1}' }}; " +
	"New-Item -ItemType Directory -Force -Path '{1}' | Out-Null; " +
	"git -C '{0}' archive --format=tar --output='{2}' origin/develop tools/bugs .agents/skills/mmo-quest-creator/scripts; " +
	"tar -x -f '{2}' -C '{1}'; " +
	"& '{3}' '{1}\tools\bugs\bug_loop.py' --repo '{0}' watch{4} *>> '{5}'; exit `$LASTEXITCODE") -f $main, $Runtime, $archive, $python, $mode, $log

$action = New-ScheduledTaskAction -Execute "powershell" -Argument ("-NoProfile -ExecutionPolicy Bypass -Command `"{0}`"" -f $command)
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -MultipleInstances IgnoreNew `
	-RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 5) -ExecutionTimeLimit ([TimeSpan]::Zero)
Register-ScheduledTask -TaskName "MMO Bug Loop" -Action $action -Trigger $trigger -Settings $settings -Force | Out-Null

Write-Host ("Registered 'MMO Bug Loop' ({0}). Start it now with: Start-ScheduledTask -TaskName 'MMO Bug Loop'" -f $(if ($DryRun) { "dry run" } else { "live" }))
