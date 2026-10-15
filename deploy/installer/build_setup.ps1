# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Compiles deploy/installer/setup.iss into AlestiaOnline-Setup.exe and prints its path.
#   pwsh deploy/installer/build_setup.ps1 -Version 1.0.1 -Launcher bin/Release/Launcher.exe

param(
	[Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
	[Parameter(Mandatory)][string]$Launcher,
	[string]$OutDir = "dist"
)

$ErrorActionPreference = "Stop"

function Find-Iscc
{
	$onPath = Get-Command iscc -ErrorAction SilentlyContinue
	if ($onPath) { return $onPath.Source }
	foreach ($candidate in @(
		"${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
		"$env:ProgramFiles\Inno Setup 6\ISCC.exe",
		"$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"))
	{
		if (Test-Path $candidate) { return $candidate }
	}
	throw "Inno Setup 6 (ISCC.exe) not found. Install it: winget install --id JRSoftware.InnoSetup -e"
}

$iscc = Find-Iscc
$launcherPath = (Resolve-Path $Launcher).Path
New-Item -ItemType Directory -Force $OutDir | Out-Null
$outPath = (Resolve-Path $OutDir).Path
$script = Join-Path $PSScriptRoot "setup.iss"

& $iscc "/DAppVersion=$Version" "/DLauncherExe=$launcherPath" "/O$outPath" "/Q" $script | Out-Host
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }

$setup = Join-Path $outPath "AlestiaOnline-Setup.exe"
if (-not (Test-Path $setup)) { throw "ISCC reported success but $setup is missing" }
$setup
