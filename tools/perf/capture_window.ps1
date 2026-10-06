# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Saves PNG screenshots of the running game client window, e.g. alongside a benchmark run, so the
# measured scene can be looked at afterwards.
#
#   powershell -File tools/perf/capture_window.ps1 -OutPrefix tools/perf/results/shot -Count 4 -IntervalSeconds 5

param(
	[string]$OutPrefix = "shot",
	[int]$Count = 1,
	[double]$IntervalSeconds = 5,
	[double]$InitialDelaySeconds = 0,
	[string]$ProcessName = "mmo_client"
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class PerfWin32 {
	[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
	[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
	[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
	[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@

[PerfWin32]::SetProcessDPIAware() | Out-Null
Start-Sleep -Milliseconds ([int]($InitialDelaySeconds * 1000))

for ($i = 0; $i -lt $Count; $i++) {
	$process = Get-Process $ProcessName -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
	if (-not $process) {
		Write-Warning "No $ProcessName window found."
		break
	}

	$rect = New-Object PerfWin32+RECT
	[PerfWin32]::GetWindowRect($process.MainWindowHandle, [ref]$rect) | Out-Null
	[PerfWin32]::SetForegroundWindow($process.MainWindowHandle) | Out-Null

	$width = $rect.Right - $rect.Left
	$height = $rect.Bottom - $rect.Top
	$bitmap = New-Object System.Drawing.Bitmap $width, $height
	$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
	$graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)

	$path = "{0}_{1}.png" -f $OutPrefix, $i
	$bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
	$graphics.Dispose()
	$bitmap.Dispose()
	Write-Output $path

	if ($i -lt $Count - 1) {
		Start-Sleep -Milliseconds ([int]($IntervalSeconds * 1000))
	}
}
