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

# Reads a header directive ("-- e2e-<name>: <value>") from a scenario's leading
# comment block. Returns the captured value, or $null if the directive is absent.
function Get-ScenarioDirective([string[]]$headerLines, [string]$name, [string]$valuePattern)
{
	foreach ($line in $headerLines)
	{
		if ($line -match "^--\s*e2e-${name}:\s*($valuePattern)")
		{
			return $Matches[1]
		}
	}
	return $null
}

$results = @()
$allGood = $true

try
{
	# Random letter suffix -> fresh character per suite run, isolated from prior state.
	$suffix = -join ((97..122) | Get-Random -Count 5 | ForEach-Object { [char]$_ })

	$first = $true
	foreach ($file in $scenarios)
	{
		$name = $file.BaseName
		$transcript = Join-Path $logDir "$name.jsonl"
		$stdout = Join-Path $logDir "$name.out.log"

		# Give the previous session's logout a moment to finish server-side; an
		# immediate reconnect can race the login/realm session cleanup.
		if (-not $first) { Start-Sleep -Milliseconds 1500 }
		$first = $false

		Write-Host "Running scenario '$name'..." -NoNewline
		$sw = [System.Diagnostics.Stopwatch]::StartNew()

		$expected = if ($expectedFailures.ContainsKey($name)) { $expectedFailures[$name] } else { 0 }
		$attempts = 0
		$exitCode = -1

		# Header directives are read once from the scenario's leading comment block.
		$headerLines = @()
		foreach ($line in Get-Content $file.FullName)
		{
			if ($line -notmatch '^--') { break }
			$headerLines += $line
		}

		# Scenarios can request a specific character class via a header directive, e.g.
		# "-- e2e-class: 1" (warrior). Each class gets its own character (names must be
		# letters only, so the class id is encoded as a letter suffix).
		$characterName = "Test$suffix"
		$classArgs = @()
		$classDirective = Get-ScenarioDirective $headerLines 'class' '\d+'
		if ($classDirective)
		{
			$classId = [int]$classDirective
			$classArgs = @("--class", "$classId")
			$characterName = "Test$suffix" + [char](97 + $classId)
		}

		# Scenarios that must not share progression state with other scenarios of the
		# same class (e.g. leveling tests) can request a dedicated character via
		# "-- e2e-own-character: <letters>". The letters are appended to the name.
		$ownCharDirective = Get-ScenarioDirective $headerLines 'own-character' '[a-z]+'
		if ($ownCharDirective)
		{
			$characterName += $ownCharDirective
		}

		# Long-running scenarios can extend the client timeout via a header directive,
		# e.g. "-- e2e-timeout: 600" (seconds). Default stays at 120.
		$timeoutSeconds = 120
		$timeoutDirective = Get-ScenarioDirective $headerLines 'timeout' '\d+'
		if ($timeoutDirective)
		{
			$timeoutSeconds = [int]$timeoutDirective
		}

		while ($true)
		{
			$attempts++

			# Each scenario runs in a fresh client process with its own character.
			$process = Start-Process -FilePath $clientExe -ArgumentList (@(
				"--config", $clientConfig,
				"--script", $file.FullName,
				"--transcript", $transcript,
				"--character", $characterName,
				"--timeout", "$timeoutSeconds"
			) + $classArgs) -WorkingDirectory $s.RepoRoot -PassThru -WindowStyle Hidden -Wait -RedirectStandardOutput $stdout

			$exitCode = $process.ExitCode
			if ($exitCode -eq $expected -or $attempts -ge 2)
			{
				break
			}

			# Retry ONCE, and only for infrastructure failures (setup 2 / timeout 3 /
			# disconnect 4). Assertion failures (exit 1) are real regressions and are
			# never retried.
			if ($exitCode -in 2, 3, 4)
			{
				Write-Host " retry (exit $exitCode)..." -NoNewline
				Start-Sleep -Seconds 3
			}
			else
			{
				break
			}
		}

		$sw.Stop()

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
			attempts   = $attempts
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
