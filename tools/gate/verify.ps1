# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Local quality gate: build -> unit tests -> E2E. Writes tools/gate/last_report.json
# and exits 0 only if every step passed. /ship refuses to merge into develop without
# a green, fresh (HEAD-matching), non-E2E-skipped report.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -SkipE2E   # quick pre-check; NOT valid for /ship

[CmdletBinding()]
param(
	[switch]$SkipE2E,
	[string[]]$Targets = @("login_server", "realm_server", "world_server", "e2e_client", "unit_tests", "game_server_unit_tests", "login_server_tests")
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$logDir = Join-Path $PSScriptRoot "logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

$steps = @()
$allPassed = $true

function Invoke-GateStep
{
	param(
		[string]$Name,
		[string]$Exe,
		[string[]]$Arguments = @()
	)

	$log = Join-Path $script:logDir ($Name + ".log")
	Write-Host ("== {0} ==" -f $Name)
	$sw = [System.Diagnostics.Stopwatch]::StartNew()
	# Out-Host keeps the command output off the pipeline so the function returns ONLY the boolean.
	& $Exe @Arguments 2>&1 | Tee-Object -FilePath $log | Out-Host
	$exit = $LASTEXITCODE
	$sw.Stop()

	$script:steps += [ordered]@{
		name = $Name
		passed = ($exit -eq 0)
		exit_code = $exit
		log = ("tools/gate/logs/{0}.log" -f $Name)
		duration_s = [math]::Round($sw.Elapsed.TotalSeconds, 1)
	}
	if ($exit -ne 0)
	{
		$script:allPassed = $false
	}
	return ($exit -eq 0)
}

$branch = (& git -C $repoRoot rev-parse --abbrev-ref HEAD)
$commit = (& git -C $repoRoot rev-parse HEAD)

Push-Location $repoRoot
try
{
	$ok = Invoke-GateStep -Name "build" -Exe "cmake" -Arguments (@("--build", "build", "--config", "Debug", "-t") + $Targets)

	if ($ok)
	{
		foreach ($test in @("unit_tests", "game_server_unit_tests", "login_server_tests"))
		{
			if (-not (Invoke-GateStep -Name $test -Exe (Join-Path $repoRoot ("bin\Debug\{0}.exe" -f $test))))
			{
				$ok = $false
				break
			}
		}
	}

	if ($ok -and -not $SkipE2E)
	{
		if (-not $env:MMO_E2E_MYSQL_PASSWORD)
		{
			Write-Host "MMO_E2E_MYSQL_PASSWORD is not set - cannot run E2E." -ForegroundColor Red
			$steps += [ordered]@{ name = "e2e"; passed = $false; exit_code = -1; log = $null; duration_s = 0 }
			$allPassed = $false
		}
		else
		{
			$null = Invoke-GateStep -Name "e2e" -Exe "powershell" -Arguments @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $repoRoot "tools\e2e\e2e_run.ps1"))
		}
	}
}
finally
{
	Pop-Location

	[ordered]@{
		branch = $branch
		commit = $commit
		timestamp = (Get-Date -Format "o")
		config = "Debug"
		e2e_skipped = [bool]$SkipE2E
		steps = $steps
		passed = $allPassed
	} | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $PSScriptRoot "last_report.json") -Encoding UTF8
}

Write-Host ""
Write-Host ("Gate result: {0} (report: tools/gate/last_report.json)" -f $(if ($allPassed) { "GREEN" } else { "RED" }))
exit $(if ($allPassed) { 0 } else { 1 })
