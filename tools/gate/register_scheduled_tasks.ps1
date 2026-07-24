# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Registers the recurring maintenance jobs in Windows Task Scheduler (current user).
# Idempotent: /F overwrites existing definitions. Re-run after changing schedules.
#
# Requires MMO_E2E_MYSQL_PASSWORD as a persistent USER environment variable
# (setx MMO_E2E_MYSQL_PASSWORD "<password>"), otherwise the nightly E2E step fails.

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

if (-not [Environment]::GetEnvironmentVariable("MMO_E2E_MYSQL_PASSWORD", "User"))
{
	Write-Warning "MMO_E2E_MYSQL_PASSWORD is not set as a user environment variable; the nightly E2E step will fail until it is (setx MMO_E2E_MYSQL_PASSWORD `"<password>`")."
}

schtasks /Create /F /TN "MMO Nightly Gate" /SC DAILY /ST 03:00 `
	/TR ("powershell -NoProfile -ExecutionPolicy Bypass -File `"{0}\tools\gate\nightly_gate.ps1`"" -f $repoRoot)

schtasks /Create /F /TN "MMO Weekly Content Audit" /SC WEEKLY /D SUN /ST 04:00 `
	/TR ("python `"{0}\tools\gate\content_audit.py`"" -f $repoRoot)

Write-Host "Scheduled tasks registered. Verify with: schtasks /Query /TN `"MMO Nightly Gate`""
