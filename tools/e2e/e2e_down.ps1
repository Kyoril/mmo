# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Tears down the E2E test stack started by e2e_up.ps1.
#
# Usage:
#   powershell -File tools/e2e/e2e_down.ps1 [-DropDb]

[CmdletBinding()]
param(
	[switch]$DropDb
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "e2e_common.psm1") -Force

$s = Get-E2eSettings
$pidFile = Join-Path $s.RuntimeDir "pids.json"

if (Test-Path $pidFile)
{
	$pids = Get-Content $pidFile -Raw | ConvertFrom-Json
	foreach ($name in @("world", "realm", "login"))
	{
		$procId = $pids.$name
		if ($procId)
		{
			$process = Get-Process -Id $procId -ErrorAction SilentlyContinue
			if ($process)
			{
				Write-Host "Stopping $name server (pid $procId)..."
				try { Stop-Process -Id $procId -Force -Confirm:$false -ErrorAction Stop } catch {}
			}
			else
			{
				Write-Host "$name server (pid $procId) is not running."
			}
		}
	}
	Remove-Item $pidFile -Force -Confirm:$false
}
else
{
	Write-Host "No pid file found at $pidFile - nothing to stop."
}

if ($DropDb)
{
	Write-Host "Dropping e2e databases..."
	Invoke-Mysql -Sql "DROP DATABASE IF EXISTS ``$($s.LoginDb)``; DROP DATABASE IF EXISTS ``$($s.RealmDb)``;" | Out-Null
}

Write-Host "E2E stack is down."
