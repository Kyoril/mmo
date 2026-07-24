# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Nightly gate run on develop. Skips (without failing) when the repo is mid-work:
# dirty tree or a non-develop branch checked out. Copies the gate report to
# tools/gate/reports/nightly-YYYY-MM-DD.json so red nights are visible later.

[CmdletBinding()]
param(
	[switch]$SkipE2E
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$reportDir = Join-Path $PSScriptRoot "reports"
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
$target = Join-Path $reportDir ("nightly-{0}.json" -f (Get-Date -Format "yyyy-MM-dd"))

$branch = (& git -C $repoRoot rev-parse --abbrev-ref HEAD)
$dirty = (& git -C $repoRoot status --porcelain)

if ($branch -ne "develop" -or $dirty)
{
	[ordered]@{
		timestamp = (Get-Date -Format "o")
		skipped = $true
		reason = ("repo busy: branch={0}, dirty={1}" -f $branch, [bool]$dirty)
		passed = $null
	} | ConvertTo-Json | Set-Content -Path $target -Encoding UTF8
	Write-Host "Nightly gate skipped: repo busy."
	exit 0
}

$gateArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "verify.ps1"))
if ($SkipE2E)
{
	$gateArgs += "-SkipE2E"
}
& powershell @gateArgs
$exit = $LASTEXITCODE
Copy-Item (Join-Path $PSScriptRoot "last_report.json") $target -Force
exit $exit
