# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Front end for bin/<config>/ui_test.exe, see tools/ui_test/README.md.
#
#   ui_test.ps1 shot [-Scenario friends] [-Glue] [-Name out.png] [-Size 1920x1080] [-Data <dir>]
#       Loads the UI once, writes artifacts/ui_test/<name> and status.json, prints a summary.
#   ui_test.ps1 start [-Scenario friends] [-Glue] [-Size ...] [-Data <dir>]
#       Starts the live app (hot reload) in the background. After every reload it rewrites
#       artifacts/ui_test/latest.png and status.json.
#   ui_test.ps1 cmd "lua FriendsFrame:IsVisible()" "screenshot open.png" ...
#       Sends commands to the live app and prints their results.
#   ui_test.ps1 status     Prints the summary of the last load.
#   ui_test.ps1 stop       Closes the live app.

param(
	[Parameter(Position = 0)][ValidateSet("shot", "start", "cmd", "status", "stop")][string]$Action = "shot",
	[Parameter(Position = 1, ValueFromRemainingArguments = $true)][string[]]$Commands,
	[string]$Scenario,
	[switch]$Glue,
	[string]$Toc,
	[string]$Name = "shot.png",
	[string]$Size,
	[string]$Data,
	[string]$Config,
	[int]$TimeoutSeconds = 60
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path "$PSScriptRoot/../..").Path
$out = Join-Path $repo "artifacts/ui_test"

function Find-Exe
{
	$configs = if ($Config) { @($Config) } else { @("Release", "RelWithDebInfo", "Debug") }
	foreach ($c in $configs)
	{
		$exe = Join-Path $repo "bin/$c/ui_test.exe"
		if (Test-Path $exe) { return $exe }
	}
	throw "ui_test.exe not found in $repo/bin. Build it: cmake --build build --config Release -t ui_test"
}

function Get-AppArgs
{
	$appArgs = @()
	if ($Glue) { $appArgs += "--glue" }
	if ($Toc) { $appArgs += @("--toc", $Toc) }
	if ($Scenario) { $appArgs += @("--scenario", $Scenario) }
	if ($Size) { $appArgs += @("--size", $Size) }
	if ($Data) { $appArgs += @("--data", $Data) }
	return $appArgs
}

function Show-Status
{
	$file = Join-Path $out "status.json"
	if (-not (Test-Path $file)) { Write-Host "No status.json yet."; return }
	$s = Get-Content $file -Raw | ConvertFrom-Json
	Write-Host ("reload #{0}  ok={1}  {2} ms  {3}" -f $s.reload, $s.ok, $s.loadMs, $s.toc)
	if ($s.screenshot) { Write-Host "screenshot: $($s.screenshot)" }
	foreach ($e in $s.errors) { Write-Host "ERROR   $e" }
	foreach ($w in $s.warnings) { Write-Host "warning $w" }
	if ($s.unmockedCalls.Count) { Write-Host ("unmocked API calls: " + ($s.unmockedCalls -join ", ")) }
	if ($s.undefinedGlobals.Count) { Write-Host ("undefined globals read: " + ($s.undefinedGlobals -join ", ")) }
}

switch ($Action)
{
	"shot"
	{
		$exe = Find-Exe
		$appArgs = (Get-AppArgs) + @("--screenshot", $Name)
		$p = Start-Process -FilePath $exe -ArgumentList $appArgs -PassThru -Wait
		Show-Status
		exit $p.ExitCode
	}
	"start"
	{
		if (Get-Process ui_test -ErrorAction SilentlyContinue) { Write-Host "ui_test is already running; use 'cmd reload' or 'stop'."; exit 0 }
		$exe = Find-Exe
		$statusFile = Join-Path $out "status.json"
		$before = if (Test-Path $statusFile) { (Get-Item $statusFile).LastWriteTime } else { [datetime]::MinValue }
		$appArgs = Get-AppArgs
		if ($appArgs.Count) { Start-Process -FilePath $exe -ArgumentList $appArgs | Out-Null } else { Start-Process -FilePath $exe | Out-Null }

		$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
		while ((Get-Date) -lt $deadline)
		{
			if ((Test-Path $statusFile) -and (Get-Item $statusFile).LastWriteTime -gt $before) { break }
			Start-Sleep -Milliseconds 200
		}
		Show-Status
	}
	"cmd"
	{
		if (-not $Commands) { throw "No commands given." }
		$inbox = Join-Path $out "commands"
		New-Item -ItemType Directory -Force $inbox | Out-Null
		$id = "{0:yyyyMMddHHmmssfff}_{1}" -f (Get-Date), $PID
		$tmp = Join-Path $inbox "$id.tmp"
		[System.IO.File]::WriteAllText($tmp, ($Commands -join "`n"), (New-Object System.Text.UTF8Encoding $false))
		Move-Item $tmp (Join-Path $inbox "$id.cmd")

		$result = Join-Path $inbox "$id.result"
		$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
		while (-not (Test-Path $result))
		{
			if ((Get-Date) -gt $deadline) { throw "No result after $TimeoutSeconds s. Is the app running (ui_test.ps1 start)?" }
			Start-Sleep -Milliseconds 100
		}
		Get-Content $result
		Remove-Item $result
	}
	"status" { Show-Status }
	"stop"
	{
		# Through the inbox, so only the app writing to this checkout's artifacts/ui_test is closed.
		$inbox = Join-Path $out "commands"
		New-Item -ItemType Directory -Force $inbox | Out-Null
		Set-Content -Path (Join-Path $inbox "zzz_stop.cmd") -Value "quit"
	}
}
