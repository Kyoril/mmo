# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Puts the unstripped Linux server binaries of one build into the local symbol store, where
# tools/symbolicate_crash.ps1 resolves Linux crash reports (pdbid=elf) against them:
#
#     <Store>\<module>.elf\<commit>\<module>        (login_server, realm_server, world_server)
#
# Source, in order:
#   1. -Archive: a local server-symbols.zip.
#   2. The nightly release built from that commit: its server-symbols.zip asset (needs gh).
#   3. -FromImage: the stripped binary out of the Docker image tagged with the commit (needs
#      docker). Images built before server-symbols.zip existed were not stripped, so that still
#      gives function names (no file:line); newer images give nothing the release does not.
#
# Usage:
#     tools\fetch_server_symbols.ps1 -Commit <sha>
#     tools\fetch_server_symbols.ps1 -ReportDir artifacts\crash-reports\<id>     # commit from crash.txt
#     tools\fetch_server_symbols.ps1 -Commit <sha> -Archive path\to\server-symbols.zip
#     tools\fetch_server_symbols.ps1 -Commit <sha> -FromImage

[CmdletBinding()]
param(
    [string]$Commit,
    # Reads the commit from the report's "Version: ... (Commit: <sha>)" line.
    [string]$ReportDir,
    [string]$Store,
    [string]$Archive,
    [string]$Repo = 'Kyoril/mmo',
    [switch]$FromImage,
    [string]$ImagePrefix = 'kyoril/mmo_',
    [string[]]$Modules = @('login_server', 'realm_server', 'world_server'),
    # Replace binaries that are already in the store.
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $Store) { $Store = Join-Path $repoRoot 'artifacts/symbols' }

if (-not $Commit) {
    if (-not $ReportDir) { throw "Provide -Commit or -ReportDir." }
    $crashFile = Join-Path $ReportDir 'crash.txt'
    if (-not (Test-Path -LiteralPath $crashFile)) { throw "Crash file not found: $crashFile" }
    $m = [regex]::Match((Get-Content -LiteralPath $crashFile -Raw), '(?m)^Version:.*\(Commit: ([0-9a-fA-F]+)\)')
    if (-not $m.Success) { throw "$crashFile names no commit (Version: ... (Commit: <sha>)). Builds before the commit was stamped into Docker images cannot be matched." }
    $Commit = $m.Groups[1].Value
}
$Commit = $Commit.ToLowerInvariant()
if ($Commit -notmatch '^[0-9a-f]{40}$') { throw "Commit must be a full 40-digit SHA (got '$Commit')." }

function Test-Elf([string]$path) {
    $fs = [System.IO.File]::OpenRead($path)
    try {
        $magic = New-Object byte[] 4
        if ($fs.Read($magic, 0, 4) -ne 4) { return $false }
        return ($magic[0] -eq 0x7F -and $magic[1] -eq 0x45 -and $magic[2] -eq 0x4C -and $magic[3] -eq 0x46)
    } finally {
        $fs.Dispose()
    }
}

function Get-Target([string]$mod) { Join-Path $Store "$mod.elf\$Commit\$mod" }

$pending = @($Modules | Where-Object { $Force -or -not (Test-Path -LiteralPath (Get-Target $_)) })
if ($pending.Count -eq 0) {
    Write-Host "All of $($Modules -join ', ') for $Commit are already in $Store."
    exit 0
}

function Install-Binary([string]$mod, [string]$source, [string]$from) {
    if (-not (Test-Elf $source)) { throw "$source is not an ELF binary." }
    $target = Get-Target $mod
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Force
    Write-Host ("{0,-13} <- {1} ({2:N1} MB)" -f $mod, $from, ((Get-Item -LiteralPath $target).Length / 1MB))
}

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("srvsym_" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
try {
    # --- 1/2: server-symbols.zip, local or from the nightly release of that commit ---------------
    if (-not $Archive -and -not $FromImage) {
        if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw "gh (GitHub CLI) not found; pass -Archive or -FromImage." }
        # No string literals in the filter: Windows PowerShell drops embedded quotes when it passes
        # arguments to native programs.
        # One row per release: tag, commit, then its asset names.
        $rows = @(gh api --paginate "repos/$Repo/releases?per_page=100" --jq '.[] | [.tag_name, .target_commitish] + [.assets[].name] | @tsv')
        if ($LASTEXITCODE -ne 0) { throw "gh api failed listing the releases of $Repo." }
        $tags = @($rows | ForEach-Object {
            $cols = ([string]$_).Split("`t")
            if ($cols.Count -ge 3 -and $cols[1] -eq $Commit -and (@($cols | Select-Object -Skip 2) -contains 'server-symbols.zip')) { $cols[0] }
        })
        if ($tags.Count -eq 0) {
            throw "No release of $Repo built from $Commit carries server-symbols.zip (not a nightly, or one from before symbols were published). Try -FromImage for function names."
        }
        Write-Host "Downloading server-symbols.zip from release $($tags[0])"
        gh release download $tags[0] -R $Repo -p 'server-symbols.zip' -D $tmp
        if ($LASTEXITCODE -ne 0) { throw "gh release download failed." }
        $Archive = Join-Path $tmp 'server-symbols.zip'
    }

    if ($Archive) {
        if (-not (Test-Path -LiteralPath $Archive)) { throw "Archive not found: $Archive" }
        $unpacked = Join-Path $tmp 'unpacked'
        Expand-Archive -LiteralPath $Archive -DestinationPath $unpacked
        foreach ($mod in $pending) {
            $source = Join-Path $unpacked "$mod.elf\$Commit\$mod"
            if (-not (Test-Path -LiteralPath $source)) { throw "$Archive has no $mod.elf/$Commit/$mod (built from another commit?)." }
            Install-Binary $mod $source (Split-Path -Leaf $Archive)
        }
        exit 0
    }

    # --- 3: out of the Docker image ----------------------------------------------------------------
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) { throw "docker not found." }
    foreach ($mod in $pending) {
        $image = "$ImagePrefix${mod}:$Commit"
        $id = docker create $image
        if ($LASTEXITCODE -ne 0) { throw "docker create $image failed." }
        $id = ([string]@($id)[-1]).Trim()
        try {
            $out = Join-Path $tmp $mod
            docker cp "${id}:/app/$mod" $out
            if ($LASTEXITCODE -ne 0) { throw "docker cp from $image failed." }
            Install-Binary $mod $out $image
        } finally {
            docker rm $id | Out-Null
        }
    }
} finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
