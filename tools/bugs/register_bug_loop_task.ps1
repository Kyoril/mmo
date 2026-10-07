# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Registers the "MMO Bug Loop" scheduled task (current user). It starts at logon and runs
# tools/bugs/bug_loop.py watch from a fresh snapshot of origin/develop in H:\mmo-bugloop-runtime,
# never from a checkout a fixer can edit: loop changes take effect only through develop.
#
# The task's action is run_bug_loop.ps1, copied from this directory to
# %LOCALAPPDATA%\mmo-bugloop\run_bug_loop.ps1 (outside every checkout). Re-run this script to
# update that copy. The launcher restarts the loop from a new snapshot whenever it exits with 75
# (each UTC day boundary); Task Scheduler's restart-on-failure ignores exit codes.
#
# Requires the user environment variables MMO_BUG_API_KEY and MMO_E2E_MYSQL_PASSWORD (and
# MMO_GATE_PYTHON, else "python" from PATH runs the loop), git push access to origin (ssh) and to
# the data submodules' GitHub remotes, and tools/bugs on origin/develop.
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

foreach ($name in @("MMO_BUG_API_KEY", "MMO_E2E_MYSQL_PASSWORD"))
{
	if (-not [Environment]::GetEnvironmentVariable($name, "User"))
	{
		Write-Warning ("{0} is not set as a user environment variable; the bug loop will fail until it is." -f $name)
	}
}
if ($env:MMO_GATE_PYTHON)
{
	$python = $env:MMO_GATE_PYTHON
}
else
{
	$python = "python"
	Write-Warning "MMO_GATE_PYTHON is not set; the task will run 'python' from PATH, which may lack the gate's packages (protobuf)."
}

$launcherDir = Join-Path $env:LOCALAPPDATA "mmo-bugloop"
New-Item -ItemType Directory -Force -Path $launcherDir | Out-Null
$launcher = Join-Path $launcherDir "run_bug_loop.ps1"
Copy-Item -Force -Path (Join-Path $PSScriptRoot "run_bug_loop.ps1") -Destination $launcher

$log = Join-Path $reportDir "bugloop-task.log"
$arguments = "-NoProfile -ExecutionPolicy Bypass -File `"{0}`" -Main `"{1}`" -Runtime `"{2}`" -Python `"{3}`" -Log `"{4}`"" -f `
	$launcher, $main, $Runtime, $python, $log
if ($DryRun)
{
	$arguments += " -DryRun"
}

$action = New-ScheduledTaskAction -Execute "powershell" -Argument $arguments
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
# The restart settings only cover a launcher that fails to start; daily restarts happen inside it.
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -MultipleInstances IgnoreNew `
	-RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 5) -ExecutionTimeLimit ([TimeSpan]::Zero)
Register-ScheduledTask -TaskName "MMO Bug Loop" -Action $action -Trigger $trigger -Settings $settings -Force | Out-Null

Write-Host ("Registered 'MMO Bug Loop' ({0}) running {1}. Start it now with: Start-ScheduledTask -TaskName 'MMO Bug Loop'" -f `
	$(if ($DryRun) { "dry run" } else { "live" }), $launcher)
