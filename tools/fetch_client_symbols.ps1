# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Puts the PDB + EXE of one published client build into the local symbol store, where
# tools/symbolicate_crash.ps1 resolves client crash reports (mmo_client.pdb module) against them.
# Mirrors tools/fetch_server_symbols.ps1, but client builds are identified by version (the
# "appVersion" a crash report carries), not by commit, and are published as two separate release
# assets:
#
#   symbols.zip     - flat zip of bin/Release/*.pdb (includes mmo_client.pdb)
#   client-bin.zip  - the packaged client, including mmo_client.exe, plus source.txt
#
# Both are produced per-nightly by .github/workflows/nightly-release.yml and published on the
# GitHub release tagged "nightly-<version>" (also matches a "Patch <version>" release title).
#
# Source, in order:
#   1. -Archive / -BinArchive: local symbols.zip / client-bin.zip paths.
#   2. The GitHub release for that version: downloads both assets (needs gh).
#
# Once both are in hand, delegates to tools/archive_symbols.ps1 for the actual store layout (it
# reads the PDB id straight out of the EXE's CodeView record, so the stored folder name always
# matches what the client reports).
#
# Usage:
#     tools\fetch_client_symbols.ps1 -Version 0.3.0.4140
#     tools\fetch_client_symbols.ps1 -ReportDir artifacts\crash-reports\<id>     # version from report.json
#     tools\fetch_client_symbols.ps1 -Version 0.3.0.4140 -Archive symbols.zip -BinArchive client-bin.zip

[CmdletBinding()]
param(
    [string]$Version,
    # Reads the version from the report's report.json "appVersion" field.
    [string]$ReportDir,
    [string]$Store,
    [string]$Archive,
    [string]$BinArchive,
    [string]$Repo = 'Kyoril/mmo',
    [string[]]$Modules = @('mmo_client')
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Store) { $Store = Join-Path $repoRoot 'artifacts/symbols' }

if (-not $Version) {
    if (-not $ReportDir) { throw "Provide -Version or -ReportDir." }
    $reportFile = Join-Path $ReportDir 'report.json'
    if (-not (Test-Path -LiteralPath $reportFile)) { throw "Report file not found: $reportFile" }
    $report = Get-Content -LiteralPath $reportFile -Raw | ConvertFrom-Json
    if (-not $report.appVersion) { throw "$reportFile has no appVersion." }
    $Version = [string]$report.appVersion
}
$Version = $Version.Trim()
if ($Version -notmatch '^[0-9]+(\.[0-9]+){2,3}$') { throw "Version doesn't look like a client version (got '$Version')." }

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("clisym_" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
try {
    # --- 1/2: symbols.zip + client-bin.zip, local or from the matching GitHub release -------------
    if (-not $Archive -or -not $BinArchive) {
        if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw "gh (GitHub CLI) not found; pass -Archive and -BinArchive." }
        # No string literals in the filter: Windows PowerShell drops embedded quotes when it passes
        # arguments to native programs.
        # One row per release: tag, then its asset names.
        $rows = @(gh api --paginate "repos/$Repo/releases?per_page=100" --jq '.[] | [.tag_name] + [.assets[].name] | @tsv')
        if ($LASTEXITCODE -ne 0) { throw "gh api failed listing the releases of $Repo." }
        $escaped = [regex]::Escape($Version)
        $tags = @($rows | ForEach-Object {
            $cols = ([string]$_).Split("`t")
            if ($cols.Count -lt 1) { return }
            $tag = $cols[0]
            if ($tag -notmatch "(^|-)$escaped$") { return }
            $assets = @($cols | Select-Object -Skip 1)
            if (($assets -contains 'symbols.zip') -and ($assets -contains 'client-bin.zip')) { $tag }
        })
        if ($tags.Count -eq 0) {
            throw "No release of $Repo tagged for version $Version carries both symbols.zip and client-bin.zip (not a nightly, or older than when symbols were published)."
        }

        if (-not $Archive) {
            Write-Host "Downloading symbols.zip from release $($tags[0])"
            gh release download $tags[0] -R $Repo -p 'symbols.zip' -D $tmp
            if ($LASTEXITCODE -ne 0) { throw "gh release download (symbols.zip) failed." }
            $Archive = Join-Path $tmp 'symbols.zip'
        }
        if (-not $BinArchive) {
            Write-Host "Downloading client-bin.zip from release $($tags[0])"
            gh release download $tags[0] -R $Repo -p 'client-bin.zip' -D $tmp
            if ($LASTEXITCODE -ne 0) { throw "gh release download (client-bin.zip) failed." }
            $BinArchive = Join-Path $tmp 'client-bin.zip'
        }
    }

    if (-not (Test-Path -LiteralPath $Archive)) { throw "Archive not found: $Archive" }
    if (-not (Test-Path -LiteralPath $BinArchive)) { throw "Archive not found: $BinArchive" }

    $pdbUnpacked = Join-Path $tmp 'pdb'
    $binUnpacked = Join-Path $tmp 'bin'
    Expand-Archive -LiteralPath $Archive -DestinationPath $pdbUnpacked
    Expand-Archive -LiteralPath $BinArchive -DestinationPath $binUnpacked

    $archiveScript = Join-Path $repoRoot 'tools/archive_symbols.ps1'
    foreach ($mod in $Modules) {
        $exe = Join-Path $binUnpacked "$mod.exe"
        $pdb = Join-Path $pdbUnpacked "$mod.pdb"
        if (-not (Test-Path -LiteralPath $exe)) { throw "$BinArchive has no $mod.exe (wrong module for a client release?)." }
        if (-not (Test-Path -LiteralPath $pdb)) { throw "$Archive has no $mod.pdb (wrong module for a client release?)." }
        & $archiveScript -Binary $exe -Pdb $pdb -Store $Store
    }
} finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
