# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Launches the game client, lets it log in (Config/RunOnce.cfg), enter the world with the selected
# character, run the in-game "benchmark" command and quit. Prints a summary and leaves the JSON report
# in the output directory.
#
# The player's Config/Config.cfg is backed up before the run and restored afterwards, so -Set overrides
# (and anything the client writes) never leak into the real configuration.
#
# Examples:
#   powershell -File tools/perf/run_benchmark.ps1 -Label baseline
#   powershell -File tools/perf/run_benchmark.ps1 -Label nossao -Set gxSsao=0,gxRenderScale=0.75
#   powershell -File tools/perf/run_benchmark.ps1 -Label ssao -Compare "on=gxSsao:1","off=gxSsao:0"
#
# -Compare runs an interleaved comparison in one session: every camera view measures every variant
# (name=cvar:value;cvar:value), alternating the order, so thermal drift affects all variants alike.
# Use it for anything smaller than ~15%: separate runs on a laptop drift by more than that.
#
# Requirements: a Config/RunOnce.cfg that logs in, and a selected realm (lastRealm) in the config. The
# client must be built with the perf capture commands (perfdump / benchmark / enterworld).

param(
	[string]$Configuration = "Release",
	[string]$Label = "run",
	# cvar overrides as "name=value" strings (works from -File invocations, unlike a hashtable)
	[string[]]$Set = @(),
	[double]$Duration = 20,
	[double]$Warmup = 8,
	[double]$Orbit = 360,
	[int]$Segments = 8,
	[int]$TimeoutSeconds = 300,
	[string]$OutDir = "",
	# Interleaved comparison variants as "name=cvar:value;cvar:value"
	[string[]]$Compare = @(),
	[double]$Dwell = 2,
	[double]$Settle = 0.75
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$binDir = Join-Path $repoRoot "bin\$Configuration"
$clientExe = Join-Path $binDir "mmo_client.exe"
if (-not (Test-Path $clientExe)) {
	throw "Client not found: $clientExe"
}

if ([string]::IsNullOrWhiteSpace($OutDir)) {
	$OutDir = Join-Path $PSScriptRoot "results"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$safeLabel = ($Label -replace '[^A-Za-z0-9_\-]', '_')
$reportPath = Join-Path $OutDir "$stamp-$safeLabel.json"
$scriptPath = Join-Path $OutDir "$stamp-$safeLabel.cfg"

$configDir = Join-Path $binDir "Config"
$configPath = Join-Path $configDir "Config.cfg"
$backupPath = Join-Path $configDir "Config.cfg.benchmark-backup"

if (Test-Path $backupPath) {
	# A previous run died before restoring. The backup is the player's real config.
	Write-Warning "Restoring Config.cfg from a leftover benchmark backup first."
	Copy-Item $backupPath $configPath -Force
	Remove-Item $backupPath -Force
}

$overrideLines = @()
foreach ($assignment in ($Set | ForEach-Object { $_ -split ',' })) {
	$parts = $assignment.Trim() -split '=', 2
	if ($parts.Count -ne 2 -or [string]::IsNullOrWhiteSpace($parts[0])) {
		throw "Invalid -Set entry '$assignment', expected name=value"
	}
	$overrideLines += "set $($parts[0].Trim()) $($parts[1].Trim())"
}

# The exec script runs after startup: settings first (so cvar change handlers fire), then enter the
# world and arm the benchmark, which quits the client once the report is written.
$execLines = @()
$execLines += $overrideLines
$execLines += "enterworld"
$variantArgs = ""
for ($i = 0; $i -lt $Compare.Count; $i++) {
	$parts = $Compare[$i] -split '=', 2
	if ($parts.Count -ne 2) {
		throw "Invalid -Compare entry '$($Compare[$i])', expected name=cvar:value;cvar:value"
	}
	$variantArgs += " n$i=$($parts[0]) v$i=$($parts[1])"
}
$execLines += "benchmark duration=$Duration warmup=$Warmup orbit=$Orbit segments=$Segments dwell=$Dwell settle=$Settle label=$safeLabel timeout=$TimeoutSeconds quit=1 out=$($reportPath -replace '\\', '/')$variantArgs"
Set-Content -Path $scriptPath -Value $execLines -Encoding ASCII

$hadConfig = Test-Path $configPath
if ($hadConfig) {
	Copy-Item $configPath $backupPath -Force
}

try {
	# Overrides that only apply at device creation (resolution, window mode) must be in the config
	# the client reads at startup, so append them there as well.
	if ($overrideLines.Count -gt 0) {
		Add-Content -Path $configPath -Value $overrideLines -Encoding ASCII
	}

	$started = Get-Date
	$process = Start-Process -FilePath $clientExe -ArgumentList @("-exec", ($scriptPath -replace '\\', '/')) -WorkingDirectory $binDir -PassThru
	if (-not $process.WaitForExit($TimeoutSeconds * 1000 + 60000)) {
		Write-Warning "Client did not finish in time, terminating it."
		Stop-Process -Id $process.Id -Force
	}
	$elapsed = (Get-Date) - $started
}
finally {
	if ($hadConfig) {
		Copy-Item $backupPath $configPath -Force
		Remove-Item $backupPath -Force
	}
	elseif (Test-Path $configPath) {
		Remove-Item $configPath -Force
	}
}

if (-not (Test-Path $reportPath)) {
	Write-Error "No report was written ($reportPath). See $binDir\Logs\Client.log."
	exit 1
}

$report = Get-Content $reportPath -Raw | ConvertFrom-Json
if (-not $report.succeeded) {
	Write-Error "Benchmark failed: $($report.error)"
	exit 1
}

if ($report.type -eq "comparison") {
	$passNames = @($report.variants[0].metrics | Where-Object { $_.name -like "GPU:*" } | ForEach-Object { $_.name })
	$rows = foreach ($variant in $report.variants) {
		$row = [ordered]@{
			Variant  = $variant.name
			FrameMs  = [math]::Round($variant.summary.frameMs.mean, 2)
			GpuFrame = [math]::Round($variant.summary.gpuFrameMs.mean, 2)
			Cpu      = [math]::Round($variant.summary.cpuMs.mean, 2)
		}
		foreach ($pass in $passNames) {
			$metric = $variant.metrics | Where-Object { $_.name -eq $pass } | Select-Object -First 1
			$row[$pass.Substring(5)] = if ($metric) { [math]::Round($metric.avgMs, 2) } else { $null }
		}
		[pscustomobject]$row
	}
	"$Label (interleaved, {0:N0}s wall)" -f $elapsed.TotalSeconds
	$rows | Format-Table -AutoSize | Out-String -Width 260
	"    report: $reportPath"
	exit 0
}

$s = $report.summary
"{0}: {1:N1} fps avg, 1% low {2:N1} fps | frame p50 {3:N2} / p95 {4:N2} ms | CPU {5:N2} ms | GPU frame {6:N2} ms | batches {7:N0} | {8:N0}s wall" -f `
	$Label, $s.avgFps, $s.onePercentLowFps, $s.frameMs.p50, $s.frameMs.p95, $s.cpuMs.mean, $s.gpuFrameMs.mean, $s.batches.mean, $elapsed.TotalSeconds

$counters = $report.counters
$report.metrics | Where-Object { $_.name -like "GPU:*" } | ForEach-Object {
	$pass = $_.name.Substring(5)
	$draws = if ($counters) { $counters."Draws: $pass" } else { $null }
	$tris = if ($counters) { $counters."Tris (k): $pass" } else { $null }
	if ($null -ne $draws) {
		"    {0,-26} {1,7:N2} ms  {2,6:N0} draws  {3,8:N0}k tris" -f $_.name, $_.avgMs, $draws, $tris
	}
	else {
		"    {0,-26} {1,7:N2} ms" -f $_.name, $_.avgMs
	}
}
"    report: $reportPath"
