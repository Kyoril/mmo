# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Runs run_benchmark.ps1 once per variant, each on top of a common base, and prints a comparison.
#
# Example:
#   powershell -File tools/perf/sweep.ps1 -Base "gxVSync=0" -Variants "base=","noshadow=RenderShadows=0","half=gxRenderScale=0.5"
#
# A variant is "label=overrides" where overrides is a comma separated list of name=value pairs
# (empty for the base itself).

param(
	[string]$Base = "gxVSync=0",
	[string[]]$Variants = @("base="),
	[double]$Duration = 20,
	[double]$Warmup = 8,
	[string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$runner = Join-Path $PSScriptRoot "run_benchmark.ps1"
$rows = @()

foreach ($variant in $Variants) {
	$separator = $variant.IndexOf('=')
	if ($separator -lt 0) {
		throw "Invalid variant '$variant', expected label=overrides"
	}

	$label = $variant.Substring(0, $separator)
	$overrides = $variant.Substring($separator + 1)
	$set = @($Base -split ',' | Where-Object { $_ }) + @($overrides -split ',' | Where-Object { $_ })

	# A failed run must not end the sweep: record it and carry on with the next variant.
	try {
		$output = & $runner -Configuration $Configuration -Label $label -Set $set -Duration $Duration -Warmup $Warmup 2>&1
	}
	catch {
		$output = @("$label failed: $_")
	}
	$output | ForEach-Object { Write-Host $_ }

	$reportLine = $output | Where-Object { "$_" -like "*report: *" } | Select-Object -Last 1
	if (-not $reportLine) {
		$rows += [pscustomobject]@{ Variant = $label; Fps = "failed" }
		continue
	}

	$report = Get-Content (("$reportLine" -split 'report: ')[1].Trim()) -Raw | ConvertFrom-Json
	$gpu = @{}
	$report.metrics | Where-Object { $_.name -like "GPU:*" } | ForEach-Object { $gpu[$_.name.Substring(5)] = $_.avgMs }

	$rows += [pscustomobject]@{
		Variant  = $label
		Fps      = [math]::Round($report.summary.avgFps, 1)
		LowFps   = [math]::Round($report.summary.onePercentLowFps, 1)
		GpuFrame = [math]::Round($report.summary.gpuFrameMs.mean, 2)
		Cpu      = [math]::Round($report.summary.cpuMs.mean, 2)
		Shadows  = [math]::Round($gpu["Shadows"], 2)
		GBuffer  = [math]::Round($gpu["GBuffer"], 2)
		Ssao     = [math]::Round($gpu["SSAO"], 2)
		Contact  = [math]::Round($gpu["ContactShadows"], 2)
		Lighting = [math]::Round($gpu["Lighting"], 2)
		Fog      = [math]::Round($gpu["Volumetric fog"], 2)
		Bloom    = [math]::Round($gpu["Bloom"], 2)
	}
}

$rows | Format-Table -AutoSize | Out-String -Width 220
