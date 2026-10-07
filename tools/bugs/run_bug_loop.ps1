# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Action of the "MMO Bug Loop" scheduled task. register_bug_loop_task.ps1 copies this file to
# %LOCALAPPDATA%\mmo-bugloop\run_bug_loop.ps1 and the task runs that copy, never a checkout a
# fixer can edit; re-registering updates the copy.
#
# Each round takes a fresh snapshot of origin/develop (tools/bugs and the protobuf schemas) into
# $Runtime and runs bug_loop.py watch from it. The loop exits with 75 at each UTC day boundary;
# this script then starts the next round from a new snapshot. Task Scheduler's restart-on-failure
# does not react to exit codes, so the restart lives here. Any other exit code ends the task.

[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][string]$Main,
	[Parameter(Mandatory = $true)][string]$Runtime,
	[Parameter(Mandatory = $true)][string]$Python,
	[Parameter(Mandatory = $true)][string]$Log,
	[switch]$DryRun
)

# Native tools write progress to stderr; that must not abort the script.
$ErrorActionPreference = "Continue"
$restartCode = 75
# Git's GNU tar (first on PATH in Git Bash setups) misreads drive-letter paths such as H:\.
$tar = Join-Path $env:SystemRoot "System32\tar.exe"
$archive = Join-Path $env:TEMP "mmo-bugloop-runtime.tar"
$env:PYTHONIOENCODING = "utf-8"
try
{
	[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
}
catch
{
	# No console under Task Scheduler; native output is then decoded with the default code page.
}

function Write-LoopLog([string]$Message)
{
	$stamp = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
	("{0} launcher: {1}" -f $stamp, $Message) | Out-File -FilePath $Log -Append -Encoding utf8
}

# Runs a native command, appends its output to the log as UTF-8 and returns its exit code.
function Invoke-Logged([string]$Exe, [string[]]$Arguments)
{
	& $Exe @Arguments 2>&1 | ForEach-Object { "$_" } | Out-File -FilePath $Log -Append -Encoding utf8
	return $LASTEXITCODE
}

# Best effort: tells the maintainer the loop stopped. Never logs the webhook URL.
function Send-StopNotice([int]$ExitCode)
{
	$hook = [Environment]::GetEnvironmentVariable("MMO_BUGLOOP_WEBHOOK", "User")
	if (-not $hook)
	{
		return
	}
	$body = @{ content = ("Bug loop stopped with exit code {0}; see tools/gate/reports/bugloop-task.log" -f $ExitCode); allowed_mentions = @{ parse = @() } } | ConvertTo-Json -Compress
	try
	{
		Invoke-RestMethod -Uri $hook -Method Post -ContentType "application/json" -UserAgent "mmo-bug-loop/1" -Body $body -TimeoutSec 10 | Out-Null
	}
	catch
	{
		Write-LoopLog ("stop notice failed: {0}" -f $_.Exception.GetType().Name)
	}
}

do
{
	$fetched = $false
	foreach ($attempt in 1..3)
	{
		$code = Invoke-Logged "git" @("-C", $Main, "fetch", "origin", "--quiet")
		if ($code -eq 0)
		{
			$fetched = $true
			break
		}
		Write-LoopLog ("git fetch failed with {0} (attempt {1} of 3)" -f $code, $attempt)
		Start-Sleep -Seconds 60
	}
	if (-not $fetched)
	{
		Write-LoopLog "giving up: origin cannot be fetched"
		Send-StopNotice 1
		exit 1
	}

	if (Test-Path $Runtime)
	{
		Remove-Item -Recurse -Force $Runtime
	}
	New-Item -ItemType Directory -Force -Path $Runtime | Out-Null
	# PowerShell 5.1 corrupts binary pipes between native commands, so the archive goes through a file.
	$code = Invoke-Logged "git" @("-C", $Main, "archive", "--format=tar", ("--output=" + $archive), "origin/develop",
		"tools/bugs", "src/shared/proto_data")
	if ($code -ne 0)
	{
		Write-LoopLog ("git archive failed with {0}" -f $code)
		Send-StopNotice 1
		exit 1
	}
	$code = Invoke-Logged $tar @("-x", "-f", $archive, "-C", $Runtime)
	if ($code -ne 0)
	{
		Write-LoopLog ("tar failed with {0}" -f $code)
		Send-StopNotice 1
		exit 1
	}

	$arguments = @((Join-Path $Runtime "tools\bugs\bug_loop.py"), "--repo", $Main, "watch")
	if ($DryRun)
	{
		$arguments += "--dry-run"
	}
	Write-LoopLog ("starting the bug loop from a snapshot of origin/develop in {0}" -f $Runtime)
	$code = Invoke-Logged $Python $arguments
	Write-LoopLog ("bug loop exited with {0}" -f $code)
} while ($code -eq $restartCode)

if ($code -ne 0)
{
	Send-StopNotice $code
}
exit $code
