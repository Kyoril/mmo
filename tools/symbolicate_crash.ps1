# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Symbolicates a downloaded crash report: turns the build-relative "mmo_client+0xRVA" frames into
# function names and source file:line locations using the local symbol store and llvm-symbolizer.
#
# The crash report's MODULES section carries the PDB id (GUID+age). We use it to find the matching
# PDB in the symbol store, and we find the matching EXE by scanning archived binaries for the one
# whose CodeView record has the same PDB id (llvm-symbolizer needs the binary as its object).
#
# Output: <ReportDir>/symbolized.txt
#
# Usage:
#     tools\symbolicate_crash.ps1 -ReportDir artifacts\crash-reports\<id>
#     tools\symbolicate_crash.ps1 -CrashFile path\to\crash.txt -SymbolStore artifacts\symbols

[CmdletBinding()]
param(
    [string]$ReportDir,
    [string]$CrashFile,
    [string]$SymbolStore,
    [string]$OutFile,
    [string]$Llvm,
    # Only symbolicate frames from these modules (our own code is what we can fix).
    [string[]]$Modules = @('mmo_client')
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

if (-not $SymbolStore) { $SymbolStore = Join-Path $repoRoot 'artifacts/symbols' }
if ($ReportDir -and -not $CrashFile) { $CrashFile = Join-Path $ReportDir 'crash.txt' }
if (-not $CrashFile) { throw "Provide -ReportDir or -CrashFile." }
if (-not (Test-Path -LiteralPath $CrashFile)) { throw "Crash file not found: $CrashFile" }
if (-not $OutFile) {
    $OutFile = if ($ReportDir) { Join-Path $ReportDir 'symbolized.txt' } else { "$CrashFile.symbolized.txt" }
}

if (-not $Llvm) {
    $cmd = Get-Command llvm-symbolizer -ErrorAction SilentlyContinue
    if ($cmd) { $Llvm = $cmd.Source }
    elseif (Test-Path 'C:\Program Files\LLVM\bin\llvm-symbolizer.exe') { $Llvm = 'C:\Program Files\LLVM\bin\llvm-symbolizer.exe' }
    else { throw "llvm-symbolizer not found. Install LLVM or pass -Llvm." }
}

# --- Minimal PE reader: extract the CodeView (RSDS) PDB id from a binary -------------------------
function Get-PdbIdFromBinary([string]$path) {
    $b = [System.IO.File]::ReadAllBytes($path)
    if ($b.Length -lt 0x40 -or $b[0] -ne 0x4D -or $b[1] -ne 0x5A) { return $null }
    $peOff = [BitConverter]::ToUInt32($b, 0x3C)
    if ($b[$peOff] -ne 0x50 -or $b[$peOff + 1] -ne 0x45) { return $null }
    $coff = $peOff + 4
    $numSections = [BitConverter]::ToUInt16($b, $coff + 2)
    $sizeOfOpt = [BitConverter]::ToUInt16($b, $coff + 16)
    $opt = $coff + 20
    $magic = [BitConverter]::ToUInt16($b, $opt)
    if ($magic -eq 0x20B) { $numDirsOff = $opt + 108; $dirStart = $opt + 112 }
    elseif ($magic -eq 0x10B) { $numDirsOff = $opt + 92; $dirStart = $opt + 96 }
    else { return $null }
    if ([BitConverter]::ToUInt32($b, $numDirsOff) -le 6) { return $null }
    $debugDir = $dirStart + 6 * 8
    $debugRva = [BitConverter]::ToUInt32($b, $debugDir)
    $debugSize = [BitConverter]::ToUInt32($b, $debugDir + 4)
    if ($debugRva -eq 0) { return $null }

    $secStart = $opt + $sizeOfOpt
    $debugOff = $null
    for ($i = 0; $i -lt $numSections; $i++) {
        $s = $secStart + $i * 40
        $va = [BitConverter]::ToUInt32($b, $s + 12)
        $vsz = [BitConverter]::ToUInt32($b, $s + 8)
        $raw = [BitConverter]::ToUInt32($b, $s + 20)
        $rsz = [BitConverter]::ToUInt32($b, $s + 16)
        $span = [Math]::Max($vsz, $rsz)
        if ($debugRva -ge $va -and $debugRva -lt ($va + $span)) { $debugOff = $raw + ($debugRva - $va); break }
    }
    if ($null -eq $debugOff) { return $null }

    $count = [int]($debugSize / 28)
    for ($i = 0; $i -lt $count; $i++) {
        $e = $debugOff + $i * 28
        if ([BitConverter]::ToUInt32($b, $e + 12) -ne 2) { continue }
        $cvOff = [BitConverter]::ToUInt32($b, $e + 24)
        if ([System.Text.Encoding]::ASCII.GetString($b, $cvOff, 4) -ne 'RSDS') { continue }
        $g = New-Object byte[] 16
        [Array]::Copy($b, $cvOff + 4, $g, 0, 16)
        $age = [BitConverter]::ToUInt32($b, $cvOff + 20)
        return ([Guid]::new($g).ToString('N')).ToUpperInvariant() + ('{0:X}' -f $age)
    }
    return $null
}

# --- Parse the crash report: module pdbids + frames ---------------------------------------------
$text = Get-Content -LiteralPath $CrashFile -Raw
$lines = $text -split "`r?`n"

$modulePdbId = @{}   # module name -> pdbid
foreach ($line in $lines) {
    $m = [regex]::Match($line, '^(\S+)\s+base=0x[0-9a-fA-F]+\s+size=0x[0-9a-fA-F]+\s+pdb=\S+\s+pdbid=(\S+)\s*$')
    if ($m.Success) { $modulePdbId[$m.Groups[1].Value] = $m.Groups[2].Value }
}

# Frames: "Frame N: module+0xRVA (0xABS) ..."
$frames = @()
foreach ($line in $lines) {
    $m = [regex]::Match($line, '^Frame\s+(\d+):\s+(\S+)\+0x([0-9a-fA-F]+)\s+\(0x([0-9a-fA-F]+)\)')
    if ($m.Success) {
        $frames += [pscustomobject]@{
            Index  = [int]$m.Groups[1].Value
            Module = $m.Groups[2].Value
            Rva    = '0x' + $m.Groups[3].Value
            Abs    = '0x' + $m.Groups[4].Value
        }
    }
}

$sb = New-Object System.Text.StringBuilder
function Emit($s) { [void]$sb.AppendLine($s) }

Emit "Symbolicated from: $CrashFile"
Emit "Symbol store     : $SymbolStore"
Emit ("Generated        : " + (Get-Date -Format 'u'))
Emit ""

# Cache of "module -> resolved {RVA -> 'func | file:line'}".
$resolvedByModule = @{}

foreach ($mod in ($frames.Module | Select-Object -Unique)) {
    if ($Modules -notcontains $mod) { continue }
    $pdbid = $modulePdbId[$mod]
    if (-not $pdbid) { Emit "[$mod] no pdbid in MODULES section - cannot symbolicate."; continue }

    $pdb = Join-Path $SymbolStore "$mod.pdb\$pdbid\$mod.pdb"
    if (-not (Test-Path -LiteralPath $pdb)) {
        Emit "[$mod] no PDB for pdbid $pdbid in store. Run tools\archive_symbols on that exact build."
        continue
    }

    # Find the matching EXE (same pdbid) so llvm-symbolizer has an object to work with.
    $exe = $null
    $exeRoot = Join-Path $SymbolStore "$mod.exe"
    if (Test-Path -LiteralPath $exeRoot) {
        foreach ($cand in (Get-ChildItem -LiteralPath $exeRoot -Recurse -Filter "$mod.exe" -ErrorAction SilentlyContinue)) {
            if ((Get-PdbIdFromBinary $cand.FullName) -eq $pdbid) { $exe = $cand.FullName; break }
        }
    }
    if (-not $exe) {
        Emit "[$mod] found the PDB but no matching EXE (pdbid $pdbid) in the store. Re-run archive_symbols WITHOUT -NoBinary on that build."
        continue
    }

    # llvm-symbolizer resolves the PDB next to the object; co-locate the exact pair in a temp dir.
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("symb_" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    try {
        Copy-Item -LiteralPath $exe -Destination (Join-Path $tmp "$mod.exe")
        Copy-Item -LiteralPath $pdb -Destination (Join-Path $tmp "$mod.pdb")
        $tmpExe = Join-Path $tmp "$mod.exe"

        $rvas = @($frames | Where-Object Module -eq $mod | Select-Object -ExpandProperty Rva -Unique)
        $addrInput = ($rvas -join "`n")
        $out = $addrInput | & $Llvm --obj=$tmpExe --relative-address --demangle 2>&1

        # llvm-symbolizer emits, per address: function line, then "file:line:col", then a blank line.
        $map = @{}
        $i = 0
        $func = $null
        foreach ($o in @($out)) {
            $o = [string]$o
            if ($o.Trim() -eq '') {
                if ($i -lt $rvas.Count) { $i++ }
                $func = $null
                continue
            }
            if ($null -eq $func) { $func = $o.Trim() }
            else {
                if ($i -lt $rvas.Count) { $map[$rvas[$i]] = "$func | $($o.Trim())" }
            }
        }
        $resolvedByModule[$mod] = $map
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Emit "-----------------------------------------------"
Emit "SYMBOLICATED STACK TRACE"
Emit "-----------------------------------------------"
foreach ($f in ($frames | Sort-Object Index)) {
    $resolved = $null
    if ($resolvedByModule.ContainsKey($f.Module)) { $resolved = $resolvedByModule[$f.Module][$f.Rva] }
    if ($resolved) {
        Emit ("Frame {0}: {1}+{2}  ->  {3}" -f $f.Index, $f.Module, $f.Rva, $resolved)
    } else {
        Emit ("Frame {0}: {1}+{2}  ({3})  [unresolved]" -f $f.Index, $f.Module, $f.Rva, $f.Abs)
    }
}

# A focused list of our own source locations, for quick triage.
Emit ""
Emit "-----------------------------------------------"
Emit "OUR SOURCE LOCATIONS (top of stack first)"
Emit "-----------------------------------------------"
# Match only paths under this repo's src/ tree (build-time paths are absolute, e.g. F:\mmo\src\...),
# so CRT/dependency frames whose paths merely contain "\src\" are not mistaken for our code.
$ourSrc = (Join-Path $repoRoot 'src').ToLowerInvariant()
$ourSrcAlt = $ourSrc.Replace('\', '/')
$any = $false
foreach ($f in ($frames | Sort-Object Index)) {
    if (-not $resolvedByModule.ContainsKey($f.Module)) { continue }
    $resolved = $resolvedByModule[$f.Module][$f.Rva]
    if ($resolved) {
        $lc = $resolved.ToLowerInvariant()
        if ($lc.Contains($ourSrc) -or $lc.Contains($ourSrcAlt)) {
            Emit ("  Frame {0}: {1}" -f $f.Index, $resolved)
            $any = $true
        }
    }
}
if (-not $any) { Emit "  (none resolved under $repoRoot\src - the crash may be in a dependency, the CRT, or symbols are missing)" }

Set-Content -LiteralPath $OutFile -Value $sb.ToString() -Encoding UTF8
Write-Host "Wrote $OutFile"
