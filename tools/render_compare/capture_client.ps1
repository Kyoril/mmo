# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Captures the client area of the running mmo_client window to a PNG.
# Usage: powershell -File tools/render_compare/capture_client.ps1 -OutFile shot.png
param(
    [Parameter(Mandatory = $true)][string]$OutFile
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class MmoCapture
{
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);
}
"@

[MmoCapture]::SetProcessDPIAware() | Out-Null
$process = Get-Process mmo_client -ErrorAction Stop | Select-Object -First 1
$hwnd = $process.MainWindowHandle
[MmoCapture]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 500

$rect = New-Object MmoCapture+RECT
[MmoCapture]::GetClientRect($hwnd, [ref]$rect) | Out-Null
$origin = New-Object MmoCapture+POINT
[MmoCapture]::ClientToScreen($hwnd, [ref]$origin) | Out-Null

$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($origin.X, $origin.Y, 0, 0, (New-Object System.Drawing.Size $width, $height))
$bitmap.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
Write-Output "Saved $OutFile ($width x $height)"
