# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Archives a built binary's PDB (and optionally the binary itself) into a local symbol store laid
# out the symsrv way:
#
#     <Store>\mmo_client.pdb\<pdbid>\mmo_client.pdb
#     <Store>\mmo_client.exe\<TimeDateStamp><SizeOfImage>\mmo_client.exe
#
# <pdbid> is the PDB GUID + age, formatted exactly as the crash reporter emits it in the MODULES
# section of a crash report, so the loop-side resolver can find the matching PDB by that id.
#
# The id is read from the binary's CodeView (RSDS) debug record - the same source dbghelp uses on
# the client - so the stored folder name always matches what the client reports, with or without a
# build pipeline.
#
# Usage (run after a manual build):
#     tools\archive_symbols.ps1
#     tools\archive_symbols.ps1 -Binary bin\Release\mmo_client.exe -Store F:\mmo-symbols
#     tools\archive_symbols.ps1 -NoBinary        # archive only the PDB

[CmdletBinding()]
param(
    # Binary to read the PDB id from. Defaults to the Release client next to the repo root.
    [string]$Binary,

    # PDB to archive. Defaults to the PDB sitting next to the binary.
    [string]$Pdb,

    # Symbol store root.
    [string]$Store,

    # Skip archiving the binary itself (PDB only).
    [switch]$NoBinary
)

$ErrorActionPreference = 'Stop'

# Repo root is the parent of this script's tools\ folder.
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

if (-not $Binary) { $Binary = Join-Path $repoRoot 'bin/Release/mmo_client.exe' }
if (-not $Store)  { $Store  = Join-Path $repoRoot 'artifacts/symbols' }

if (-not (Test-Path -LiteralPath $Binary)) {
    throw "Binary not found: $Binary"
}
$Binary = (Resolve-Path -LiteralPath $Binary).Path

# --- Read a little-endian value out of the byte array -------------------------------------------
function Get-U16([byte[]]$b, [int]$o) { [BitConverter]::ToUInt16($b, $o) }
function Get-U32([byte[]]$b, [int]$o) { [BitConverter]::ToUInt32($b, $o) }

# --- Parse the PE to find the CodeView (RSDS) debug record --------------------------------------
$bytes = [System.IO.File]::ReadAllBytes($Binary)

if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
    throw "Not a PE image (missing MZ header): $Binary"
}

$peOff = Get-U32 $bytes 0x3C
if ($bytes[$peOff] -ne 0x50 -or $bytes[$peOff + 1] -ne 0x45) {
    throw "Invalid PE signature in $Binary"
}

$coff       = $peOff + 4
$timeStamp  = Get-U32 $bytes ($coff + 4)
$numSections = Get-U16 $bytes ($coff + 2)
$sizeOfOpt  = Get-U16 $bytes ($coff + 16)
$opt        = $coff + 20

$magic = Get-U16 $bytes $opt
switch ($magic) {
    0x20B { $sizeOfImage = Get-U32 $bytes ($opt + 56); $numDirsOff = $opt + 108; $dirStart = $opt + 112 }  # PE32+
    0x10B { $sizeOfImage = Get-U32 $bytes ($opt + 56); $numDirsOff = $opt + 92;  $dirStart = $opt + 96  }  # PE32
    default { throw "Unknown optional header magic 0x{0:X} in $Binary" -f $magic }
}

$numDirs = Get-U32 $bytes $numDirsOff
if ($numDirs -le 6) {
    throw "No debug data directory present in $Binary (built without /DEBUG?)"
}

# Data directory index 6 = IMAGE_DIRECTORY_ENTRY_DEBUG.
$debugDir     = $dirStart + 6 * 8
$debugRva     = Get-U32 $bytes $debugDir
$debugSize    = Get-U32 $bytes ($debugDir + 4)
if ($debugRva -eq 0 -or $debugSize -eq 0) {
    throw "No debug directory in $Binary (built without PDB?)"
}

# Section headers follow the optional header; use them to map RVA -> file offset.
$secStart = $opt + $sizeOfOpt
function Rva-ToOffset([uint32]$rva) {
    for ($i = 0; $i -lt $script:numSections; $i++) {
        $s   = $script:secStart + $i * 40
        $va  = Get-U32 $script:bytes ($s + 12)
        $vsz = Get-U32 $script:bytes ($s + 8)
        $raw = Get-U32 $script:bytes ($s + 20)
        $rsz = Get-U32 $script:bytes ($s + 16)
        $span = [Math]::Max($vsz, $rsz)
        if ($rva -ge $va -and $rva -lt ($va + $span)) {
            return $raw + ($rva - $va)
        }
    }
    throw "RVA 0x{0:X} not found in any section" -f $rva
}

$debugOff = Rva-ToOffset $debugRva

# Walk IMAGE_DEBUG_DIRECTORY entries (28 bytes each) looking for type 2 = CODEVIEW.
$guid = $null; $age = $null; $pdbPath = $null
$count = [int]($debugSize / 28)
for ($i = 0; $i -lt $count; $i++) {
    $e    = $debugOff + $i * 28
    $type = Get-U32 $bytes ($e + 12)
    if ($type -ne 2) { continue }

    $cvOff = Get-U32 $bytes ($e + 24)   # PointerToRawData (file offset of the CV record)
    # RSDS signature.
    if ([System.Text.Encoding]::ASCII.GetString($bytes, $cvOff, 4) -ne 'RSDS') { continue }

    $guidBytes = New-Object byte[] 16
    [Array]::Copy($bytes, $cvOff + 4, $guidBytes, 0, 16)
    $guid = [Guid]::new($guidBytes)
    $age  = Get-U32 $bytes ($cvOff + 20)

    # Null-terminated PDB path follows the age.
    $p = $cvOff + 24
    $end = $p
    while ($end -lt $bytes.Length -and $bytes[$end] -ne 0) { $end++ }
    $pdbPath = [System.Text.Encoding]::ASCII.GetString($bytes, $p, $end - $p)
    break
}

if (-not $guid) {
    throw "No RSDS CodeView record found in $Binary"
}

# pdbid = GUID (32 upper-case hex, no dashes) + age in upper-case hex with no leading zeros.
$pdbId = ($guid.ToString('N')).ToUpperInvariant() + ('{0:X}' -f $age)

# --- Resolve the PDB to archive -----------------------------------------------------------------
if (-not $Pdb) {
    $pdbName = Split-Path -Leaf $pdbPath
    $candidate = Join-Path (Split-Path -Parent $Binary) $pdbName
    if (Test-Path -LiteralPath $candidate) {
        $Pdb = $candidate
    } else {
        throw "Could not find '$pdbName' next to the binary. Pass -Pdb explicitly."
    }
}
if (-not (Test-Path -LiteralPath $Pdb)) {
    throw "PDB not found: $Pdb"
}
$Pdb = (Resolve-Path -LiteralPath $Pdb).Path
$pdbLeaf = Split-Path -Leaf $Pdb

Write-Host "Binary : $Binary"
Write-Host "PDB    : $Pdb"
Write-Host "pdbid  : $pdbId   (matches the MODULES section of crash reports)"
Write-Host "Store  : $Store"
Write-Host ""

# --- Copy the PDB into the store ----------------------------------------------------------------
$pdbDest = Join-Path (Join-Path (Join-Path $Store $pdbLeaf) $pdbId) $pdbLeaf
if (Test-Path -LiteralPath $pdbDest) {
    Write-Host "PDB already archived: $pdbDest"
} else {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $pdbDest) | Out-Null
    Copy-Item -LiteralPath $Pdb -Destination $pdbDest
    Write-Host "Archived PDB -> $pdbDest"
}

# --- Optionally copy the binary into the store (helps llvm-symbolizer) --------------------------
if (-not $NoBinary) {
    $binLeaf = Split-Path -Leaf $Binary
    $binKey  = ('{0:X8}{1:X}' -f $timeStamp, $sizeOfImage)   # symsrv binary key: TimeDateStamp + SizeOfImage
    $binDest = Join-Path (Join-Path (Join-Path $Store $binLeaf) $binKey) $binLeaf
    if (Test-Path -LiteralPath $binDest) {
        Write-Host "Binary already archived: $binDest"
    } else {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $binDest) | Out-Null
        Copy-Item -LiteralPath $Binary -Destination $binDest
        Write-Host "Archived binary -> $binDest"
    }
}

Write-Host ""
Write-Host "Done."
