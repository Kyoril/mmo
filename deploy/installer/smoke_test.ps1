# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Silent install / upgrade / uninstall checks for AlestiaOnline-Setup.exe. Exit 0 = all passed.
#   pwsh deploy/installer/smoke_test.ps1 -Setup dist/AlestiaOnline-Setup.exe -Launcher bin/Release/Launcher.exe
#   -AllUsers additionally installs for all users (needs an elevated shell; CI runners are).
# The test uses the real AppId, so it refuses to run (exit 1) when a real Alestia Online install is
# registered (HKCU, and HKLM with -AllUsers): uninstall that one first.

param(
	[Parameter(Mandatory)][string]$Setup,
	[Parameter(Mandatory)][string]$Launcher,
	[switch]$AllUsers
)

$ErrorActionPreference = "Stop"
$Setup = (Resolve-Path $Setup).Path
$expectedHash = (Get-FileHash -Algorithm SHA256 (Resolve-Path $Launcher).Path).Hash
$failures = 0

$uninstallKey = "Software\Microsoft\Windows\CurrentVersion\Uninstall\{032178BC-35CD-47F5-BA65-309AF9F74D43}_is1"
$existing = @("HKCU:\$uninstallKey")
if ($AllUsers) { $existing += "HKLM:\$uninstallKey" }
foreach ($key in $existing)
{
	if (Test-Path $key)
	{
		Write-Host "ABORT  a real Alestia Online install is registered ($key); the smoke test would overwrite it. Uninstall it first."
		exit 1
	}
}

function Invoke-Setup([string]$dir, [string]$scope)
{
	$p = Start-Process -FilePath $Setup -Wait -PassThru -ArgumentList @(
		"/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", $scope, '/MERGETASKS="!desktopicon"', "/DIR=""$dir""")
	if ($p.ExitCode -ne 0) { throw "setup exited with $($p.ExitCode)" }
}

function Invoke-Uninstall([string]$dir)
{
	$uninstaller = Join-Path $dir "unins000.exe"
	$data = Join-Path $dir "unins000.dat"
	if (-not (Test-Path $uninstaller)) { throw "no uninstaller in $dir" }
	# unins000.exe copies itself to %TEMP% and returns at once; wait for the real post-condition:
	# uninstaller files gone and the folder either gone or no longer changing.
	Start-Process -FilePath $uninstaller -Wait -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART")
	$deadline = (Get-Date).AddSeconds(60)
	$lastSnapshot = $null
	$stableSince = $null
	while ($true)
	{
		if ((Get-Date) -gt $deadline) { throw "uninstall did not finish within 60 s ($dir)" }
		if (-not (Test-Path $uninstaller) -and -not (Test-Path $data))
		{
			if (-not (Test-Path $dir)) { break }
			# The uninstaller may remove the folder between Test-Path and the listing.
			$entries = Get-ChildItem -Recurse -Force -Name $dir -ErrorAction SilentlyContinue
			if (-not (Test-Path $dir)) { break }
			$snapshot = ($entries | Sort-Object) -join "|"
			if ($snapshot -eq $lastSnapshot)
			{
				if (((Get-Date) - $stableSince).TotalSeconds -ge 1) { break }
			}
			else { $lastSnapshot = $snapshot; $stableSince = Get-Date }
		}
		Start-Sleep -Milliseconds 250
	}
	# Inno deletes the Apps & Features entry first; a manual test once saw it survive an uninstall
	# that removed everything else (not reproduced), so the entry is part of the post-condition.
	foreach ($root in "HKCU:", "HKLM:")
	{
		if (Test-Path "$root\$uninstallKey") { throw "Apps & Features entry left behind: $root\$uninstallKey" }
	}
}

function Add-FakeDownload([string]$dir)
{
	# Stand-ins for what the launcher and client put into the folder.
	New-Item -ItemType Directory -Force (Join-Path $dir "Data\Locales"), (Join-Path $dir "Logs"), (Join-Path $dir "Config"), (Join-Path $dir "Cache") | Out-Null
	foreach ($f in "Data\Models.hpak", "Data\Locales\enUS.hpak", "mmo_client.exe", "mmo_error.exe", "fmod.dll", "launcher.ini", "Launcher.exe.1760000000000", "Logs\Client.log", "Config\Config.cfg", "Cache\Items.db")
	{
		Set-Content -Path (Join-Path $dir $f) -Value "x"
	}
}

function Get-AllowedRights([string]$path, [string]$sid, [switch]$ExplicitOnly)
{
	# Union of the Allow rights the ACL of $path grants the SID, optionally ignoring inherited ACEs.
	$rights = [System.Security.AccessControl.FileSystemRights]0
	$rules = (Get-Acl $path).GetAccessRules($true, -not $ExplicitOnly, [System.Security.Principal.SecurityIdentifier])
	foreach ($rule in $rules)
	{
		if ($rule.IdentityReference.Value -eq $sid -and $rule.AccessControlType -eq "Allow")
		{
			$rights = $rights -bor $rule.FileSystemRights
		}
	}
	return $rights
}

$usersSid = "S-1-5-32-545"   # BUILTIN\Users
$modifyRight = [System.Security.AccessControl.FileSystemRights]::Modify
$writeRights = [System.Security.AccessControl.FileSystemRights]::Write -bor [System.Security.AccessControl.FileSystemRights]::Delete -bor [System.Security.AccessControl.FileSystemRights]::ChangePermissions -bor [System.Security.AccessControl.FileSystemRights]::TakeOwnership

function Test-Scenario([string]$name, [scriptblock]$body)
{
	try { & $body; Write-Host "PASS  $name" }
	catch { Write-Host "FAIL  $name -- $_"; $script:failures++ }
}

function New-TestDir([string]$suffix)
{
	# Space and non-ASCII on purpose: player profile paths often have both.
	$dir = Join-Path ([IO.Path]::GetTempPath()) "Alestia Önline Smoke $suffix $([guid]::NewGuid().ToString('N').Substring(0, 6))"
	return $dir
}

Test-Scenario "install, then uninstall removes everything" {
	$dir = New-TestDir "clean"
	Invoke-Setup $dir "/CURRENTUSER"
	$installed = Join-Path $dir "Launcher.exe"
	if (-not (Test-Path $installed)) { throw "Launcher.exe missing" }
	if ((Get-FileHash -Algorithm SHA256 $installed).Hash -ne $expectedHash) { throw "Launcher.exe differs from the packaged binary" }
	# Users-Modify is for all-users installs only: in a profile it would let other accounts swap Launcher.exe.
	if (((Get-AllowedRights $dir $usersSid -ExplicitOnly) -band $writeRights) -ne 0) { throw "per-user install grants Users an explicit write right on $dir" }
	Add-FakeDownload $dir
	Invoke-Uninstall $dir
	if (Test-Path $dir) { throw "folder left behind: $((Get-ChildItem -Recurse -Name $dir) -join ', ')" }
}

Test-Scenario "upgrade keeps downloaded data" {
	$dir = New-TestDir "upgrade"
	Invoke-Setup $dir "/CURRENTUSER"
	Add-FakeDownload $dir
	Invoke-Setup $dir "/CURRENTUSER"
	foreach ($f in "Data\Models.hpak", "mmo_client.exe", "Config\Config.cfg")
	{
		if (-not (Test-Path (Join-Path $dir $f))) { throw "$f lost on upgrade" }
	}
	if (Test-Path (Join-Path $dir "unins001.exe")) { throw "second uninstaller: AppId did not match the existing install" }
	Invoke-Uninstall $dir
	if (Test-Path $dir) { throw "folder left behind after upgrade + uninstall" }
}

Test-Scenario "foreign file survives uninstall" {
	$dir = New-TestDir "foreign"
	New-Item -ItemType Directory -Force $dir | Out-Null
	Set-Content -Path (Join-Path $dir "foreign.txt") -Value "not ours"
	Invoke-Setup $dir "/CURRENTUSER"
	Add-FakeDownload $dir
	Invoke-Uninstall $dir
	if (-not (Test-Path (Join-Path $dir "foreign.txt"))) { throw "uninstall deleted a file it did not own" }
	foreach ($f in "Launcher.exe", "Data", "mmo_client.exe", "Config")
	{
		if (Test-Path (Join-Path $dir $f)) { throw "$f left behind" }
	}
	Remove-Item -Recurse -Force $dir
}

if ($AllUsers)
{
	Test-Scenario "all-users install is writable for Users, its uninstaller is not" {
		$dir = Join-Path $env:ProgramFiles "Alestia Online Smoke $([guid]::NewGuid().ToString('N').Substring(0, 6))"
		Invoke-Setup $dir "/ALLUSERS"
		if (((Get-AllowedRights $dir $usersSid) -band $modifyRight) -ne $modifyRight) { throw "Users have no Modify right on $dir" }
		# Apps & Features runs the uninstaller elevated: a user who could replace it would get admin.
		foreach ($f in "unins000.exe", "unins000.dat")
		{
			$path = Join-Path $dir $f
			if (-not (Test-Path $path)) { throw "$f missing" }
			$granted = (Get-AllowedRights $path $usersSid) -band $writeRights
			if ($granted -ne 0) { throw "Users can write $f ($granted)" }
		}
		Invoke-Uninstall $dir
		if (Test-Path $dir) { throw "folder left behind" }
	}
}

if ($failures -gt 0) { Write-Host "$failures scenario(s) failed"; exit 1 }
Write-Host "all scenarios passed"
exit 0
