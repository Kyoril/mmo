# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Fetches crash reports from the crash report backend and downloads any that are not already present
# locally. The local folder (artifacts/crash-reports, gitignored) acts as both state and backup: a
# report is considered "already downloaded" if its folder exists, so re-runs only pull new reports.
#
# Layout produced per report:
#     artifacts/crash-reports/<id>/report.json   - metadata from /api/reports/:id
#     artifacts/crash-reports/<id>/crash.txt      - the crash log from /api/reports/:id/download
#     artifacts/crash-reports/<id>/STATUS         - workflow state (starts as "downloaded")
#
# This script does NOT analyze, fix, or delete anything. It is the deterministic data-fetch step.

[CmdletBinding()]
param(
    [string]$ApiBase = 'https://error.mmo-dev.net',
    [string]$Dest,
    [int]$PageSize = 50,
    # Emit a machine-readable JSON summary on the last line (consumed by the fix workflow).
    [switch]$Json
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Dest) { $Dest = Join-Path $repoRoot 'artifacts/crash-reports' }
New-Item -ItemType Directory -Force -Path $Dest | Out-Null

function Get-ReportId($r) {
    if ($r._id) { return [string]$r._id }
    if ($r.id)  { return [string]$r.id }
    return $null
}

# --- Page through the report list ---------------------------------------------------------------
$all = @()
$page = 1
do {
    $url = "$ApiBase/api/reports?page=$page&limit=$PageSize"
    $resp = Invoke-RestMethod -Uri $url -Method Get -TimeoutSec 30
    if ($resp.reports) { $all += $resp.reports }
    $pages = if ($resp.pagination) { [int]$resp.pagination.pages } else { 1 }
    $page++
} while ($page -le $pages -and $pages -gt 0)

$newIds = @()
$skipped = 0

foreach ($r in $all) {
    $id = Get-ReportId $r
    if (-not $id) { continue }

    $reportDir = Join-Path $Dest $id
    if (Test-Path -LiteralPath $reportDir) {
        # Already have it - this is our dedupe/state check.
        $skipped++
        continue
    }

    Write-Host "Downloading new report $id ..."
    New-Item -ItemType Directory -Force -Path $reportDir | Out-Null

    try {
        # Full metadata.
        $details = Invoke-RestMethod -Uri "$ApiBase/api/reports/$id" -Method Get -TimeoutSec 30
        $details | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $reportDir 'report.json') -Encoding UTF8

        # The crash log / report file itself.
        Invoke-WebRequest -Uri "$ApiBase/api/reports/$id/download" -Method Get -TimeoutSec 60 `
            -OutFile (Join-Path $reportDir 'crash.txt') | Out-Null

        Set-Content -LiteralPath (Join-Path $reportDir 'STATUS') -Value 'downloaded' -Encoding ASCII
        $newIds += $id
    } catch {
        Write-Warning "Failed to download report ${id}: $($_.Exception.Message)"
        # Leave a marker so we do not silently re-treat a partial download as complete next run.
        Set-Content -LiteralPath (Join-Path $reportDir 'STATUS') -Value 'download-failed' -Encoding ASCII
    }
}

Write-Host ""
Write-Host "Total on server : $($all.Count)"
Write-Host "Newly downloaded: $($newIds.Count)"
Write-Host "Already present : $skipped"

if ($Json) {
    $summary = [ordered]@{
        totalOnServer  = $all.Count
        newlyDownloaded = $newIds.Count
        alreadyPresent = $skipped
        newIds         = $newIds
        dest           = (Resolve-Path -LiteralPath $Dest).Path
    }
    Write-Host "JSON_SUMMARY: $($summary | ConvertTo-Json -Compress)"
}
