# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Runs one E2E scenario repeatedly against an already-running stack and keeps every transcript,
# so an intermittent failure can be measured instead of argued about.
#
# A single red run says nothing on its own: crypt_wing_bosses and instant_cast_swing_timer both
# failed at rates (roughly 1 in 5 and 1 in 6) where "machine load" and "real bug" look identical
# from one sample. Twelve runs with their transcripts side by side tell them apart.
#
# Usage:
#   powershell -File tools/e2e/e2e_run.ps1 -Scenario <name> -KeepStack   # bring the stack up once
#   powershell -File tools/e2e/repro_loop.ps1 -Scenario <name> -Runs 12
#   powershell -File tools/e2e/e2e_down.ps1
#
# Transcripts land in e2e/runtime/repro/<scenario>.<run>.<pass|FAIL>.jsonl.

[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)][string]$Scenario,
	[ValidateRange(1, 200)][int]$Runs = 12,
	[ValidateSet("Debug", "RelWithDebInfo", "Release")][string]$BuildConfig = "Debug",
	[string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "e2e_common.psm1") -Force

$s = Get-E2eSettings
if (-not $OutDir) { $OutDir = Join-Path $s.RuntimeDir "repro" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$transcript = Join-Path $s.RuntimeDir "logs\$Scenario.jsonl"
$failures = 0

for ($i = 1; $i -le $Runs; $i++)
{
	& powershell -File (Join-Path $PSScriptRoot "e2e_run.ps1") `
		-Scenario $Scenario -BuildConfig $BuildConfig -NoStack -KeepStack | Out-Null
	$exitCode = $LASTEXITCODE

	# "pass" here means the runner got the result it expected, which for a negative control
	# like always_fails is a non-zero exit.
	$verdict = if ($exitCode -eq 0) { "pass" } else { $failures++; "FAIL" }
	Copy-Item $transcript (Join-Path $OutDir "$Scenario.$i.$verdict.jsonl") -Force
	Write-Host "run $i : $verdict (exit $exitCode)"
}

Write-Host "TOTAL FAILURES: $failures / $Runs  (transcripts in $OutDir)"
