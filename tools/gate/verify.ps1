# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Local quality gate: protocol version check -> build -> unit tests -> E2E. Writes tools/gate/last_report.json
# and exits 0 only if every step passed. /ship refuses to merge into develop without
# a green, fresh (HEAD-matching), non-E2E-skipped report.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -SkipE2E   # quick pre-check; NOT valid for /ship

[CmdletBinding()]
param(
	[switch]$SkipE2E,
	# all_tests is an aggregate that every mmo_add_test() suite adds itself to, so a new
	# test suite never needs an edit here.
	[string[]]$Targets = @("login_server", "realm_server", "world_server", "e2e_client", "all_tests")
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$logDir = Join-Path $PSScriptRoot "logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

$steps = @()
$allPassed = $true
$gateCompleted = $false

function Invoke-GateStep
{
	param(
		[string]$Name,
		[string]$Exe,
		[string[]]$Arguments = @(),
		# ctest has to run from the build directory. Its --test-dir flag would avoid this
		# but needs CMake 3.20, and the project floor is 3.12.
		[string]$WorkingDirectory
	)

	$log = Join-Path $script:logDir ($Name + ".log")
	Write-Host ("== {0} ==" -f $Name)
	$sw = [System.Diagnostics.Stopwatch]::StartNew()

	try
	{
		# Pass/fail must rest on exit codes, not stderr chatter, so native invocation runs
		# under 'Continue'. Scoped narrowly and restored immediately after.
		$previousEap = $ErrorActionPreference
		$ErrorActionPreference = "Continue"
		$pushed = $false
		try
		{
			if ($WorkingDirectory)
			{
				Push-Location $WorkingDirectory -ErrorAction Stop
				$pushed = $true
			}

			# Out-Host keeps the command output off the pipeline so the function returns ONLY the boolean.
			& $Exe @Arguments 2>&1 | Tee-Object -FilePath $log | Out-Host
		}
		finally
		{
			if ($pushed)
			{
				Pop-Location
			}
			$ErrorActionPreference = $previousEap
		}
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
	catch
	{
		$sw.Stop()
		Add-Content -Path $log -Value ("EXCEPTION: {0}" -f $_.Exception.Message)

		$script:steps += [ordered]@{
			name = $Name
			passed = $false
			exit_code = -1
			log = ("tools/gate/logs/{0}.log" -f $Name)
			duration_s = [math]::Round($sw.Elapsed.TotalSeconds, 1)
		}
		$script:allPassed = $false
		return $false
	}
}

$branch = (& git -C $repoRoot rev-parse --abbrev-ref HEAD)
$commit = (& git -C $repoRoot rev-parse HEAD)

Push-Location $repoRoot
try
{
	# First, and before the build, because it costs milliseconds and answers a question the
	# build cannot: did the wire format change without the version bump that makes incompatible
	# peers get rejected instead of silently misparsing each other?
	$ok = Invoke-GateStep -Name "protocol" -Exe "python" -Arguments @("tools/protocol_version_check.py")

	# After the check, not before: when the manifest is simply stale the step above says so
	# in one clear line, and that is the common case. These tests are here for the case that
	# check cannot report on -- the checker itself quietly breaking and passing everything.
	if ($ok)
	{
		$ok = Invoke-GateStep -Name "protocol_tests" -Exe "python" -Arguments @("tools/tests/test_protocol_version_check.py")
	}

	# Discovery rather than a list of filenames: a tool test that nobody runs rots silently,
	# and the one thing worse than an untested tool is a tool with tests that stopped being
	# true. This re-runs the protocol tests above as part of the sweep, which costs
	# milliseconds and keeps the step free of exclusions. Named separately so a failure here
	# does not read as the protocol checker itself being broken.
	if ($ok)
	{
		$ok = Invoke-GateStep -Name "tool_tests" -Exe "python" -Arguments @("-m", "unittest", "discover", "-s", "tools/tests", "-p", "test_*.py")
	}

	if ($ok)
	{
		$ok = Invoke-GateStep -Name "build" -Exe "cmake" -Arguments (@("--build", "build", "--config", "Debug", "-t") + $Targets)
	}

	if ($ok)
	{
		# One ctest run covers every suite mmo_add_test() registered, so a new suite needs no
		# edit here. --output-on-failure names the failing suite and prints its output into
		# tools/gate/logs/tests.log.
		$ok = Invoke-GateStep -Name "tests" -Exe "ctest" -Arguments @("-C", "Debug", "--output-on-failure") -WorkingDirectory (Join-Path $repoRoot "build")
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

	$gateCompleted = $true
}
finally
{
	Pop-Location

	# Belt-and-suspenders: any crash that skips the rest of the try block leaves
	# $gateCompleted false, so the report is red even if $allPassed never flipped.
	$reportPassed = ($allPassed -and $gateCompleted)

	[ordered]@{
		branch = $branch
		commit = $commit
		timestamp = (Get-Date -Format "o")
		config = "Debug"
		e2e_skipped = [bool]$SkipE2E
		steps = $steps
		passed = $reportPassed
	} | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $PSScriptRoot "last_report.json") -Encoding UTF8
}

Write-Host ""
Write-Host ("Gate result: {0} (report: tools/gate/last_report.json)" -f $(if ($reportPassed) { "GREEN" } else { "RED" }))
exit $(if ($reportPassed) { 0 } else { 1 })
