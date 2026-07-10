# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Runs E2E scenarios against a freshly brought-up test stack and reports a summary.
# This is the single entry point for automated end-to-end verification.
#
# Usage:
#   powershell -File tools/e2e/e2e_run.ps1                       # run all scenarios
#   powershell -File tools/e2e/e2e_run.ps1 -Scenario item_grant  # run one scenario
#   powershell -File tools/e2e/e2e_run.ps1 -KeepStack            # leave the stack running
#
# Exit code: 0 if every scenario had its expected result, 1 otherwise.

[CmdletBinding()]
param(
	[ValidateSet("Debug", "RelWithDebInfo", "Release")]
	[string]$BuildConfig = "Debug",
	[string]$Scenario = "",
	[switch]$KeepStack,
	[switch]$NoStack
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "e2e_common.psm1") -Force

$s = Get-E2eSettings
$binDir = Join-Path $s.RepoRoot "bin\$BuildConfig"
$clientExe = Join-Path $binDir "e2e_client.exe"
$scenarioDir = Join-Path $s.RepoRoot "e2e\scenarios"
$logDir = Join-Path $s.RuntimeDir "logs"
$clientConfig = Join-Path $s.RuntimeDir "e2e_client.json"

# Scenarios that are EXPECTED to fail (negative controls proving the failure plumbing).
$expectedFailures = @{ "always_fails" = 1 }

if (-not (Test-Path $clientExe))
{
	throw "e2e_client.exe not found in $binDir. Build it first: cmake --build build -t e2e_client --config $BuildConfig"
}

$scenarios = if ($Scenario)
{
	$path = Join-Path $scenarioDir "$Scenario.lua"
	if (-not (Test-Path $path)) { throw "Scenario not found: $path" }
	@(Get-Item $path)
}
else
{
	@(Get-ChildItem -Path $scenarioDir -Filter "*.lua" | Sort-Object Name)
}

if ($scenarios.Count -eq 0)
{
	throw "No scenarios found in $scenarioDir"
}

if (-not $NoStack)
{
	Write-Host "Bringing up e2e stack..."
	& (Join-Path $PSScriptRoot "e2e_up.ps1") -BuildConfig $BuildConfig
}

$results = @()
$allGood = $true

try
{
	# Random letter suffix -> fresh character per suite run, isolated from prior state.
	$suffix = -join ((97..122) | Get-Random -Count 5 | ForEach-Object { [char]$_ })

	foreach ($file in $scenarios)
	{
		$name = $file.BaseName
		$transcript = Join-Path $logDir "$name.jsonl"
		$stdout = Join-Path $logDir "$name.out.log"

		Write-Host "Running scenario '$name'..." -NoNewline
		$sw = [System.Diagnostics.Stopwatch]::StartNew()

		# Each scenario runs in a fresh client process with its own character.
		$process = Start-Process -FilePath $clientExe -ArgumentList @(
			"--config", $clientConfig,
			"--script", $file.FullName,
			"--transcript", $transcript,
			"--character", "Test$suffix",
			"--timeout", "120"
		) -WorkingDirectory $s.RepoRoot -PassThru -WindowStyle Hidden -Wait -RedirectStandardOutput $stdout

		$sw.Stop()
		$exitCode = $process.ExitCode

		$expected = if ($expectedFailures.ContainsKey($name)) { $expectedFailures[$name] } else { 0 }
		$ok = ($exitCode -eq $expected)
		if (-not $ok) { $allGood = $false }

		$statusText = if ($ok) { "PASS" } else { "FAIL" }
		$detail = if ($expected -ne 0) { " (negative control, expected exit $expected)" } else { "" }
		Write-Host (" {0} (exit {1}, {2:n1}s){3}" -f $statusText, $exitCode, $sw.Elapsed.TotalSeconds, $detail)

		$results += [ordered]@{
			scenario   = $name
			exit_code  = $exitCode
			expected   = $expected
			passed     = $ok
			duration_s = [math]::Round($sw.Elapsed.TotalSeconds, 1)
			transcript = $transcript
		}
	}
}
finally
{
	[ordered]@{
		timestamp = (Get-Date -Format "o")
		all_passed = $allGood
		results = $results
	} | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $logDir "summary.json") -Encoding UTF8

	if (-not $KeepStack -and -not $NoStack)
	{
		Write-Host "Tearing down e2e stack..."
		& (Join-Path $PSScriptRoot "e2e_down.ps1")
	}
}

Write-Host ""
Write-Host ("Suite result: {0} ({1}/{2} scenarios as expected). Summary: {3}" -f `
	$(if ($allGood) { "GREEN" } else { "RED" }), `
	@($results | Where-Object { $_.passed }).Count, $results.Count, (Join-Path $logDir "summary.json"))

exit $(if ($allGood) { 0 } else { 1 })
