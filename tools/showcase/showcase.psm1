<#
.SYNOPSIS
	Helpers for staging and capturing game screenshots: headless party bots on the dev stack,
	a live command channel to them, and frameless captures of the real client.

.DESCRIPTION
	The rendered client is one real mmo_client.exe. Every other character in the shot is an
	e2e_client running tools/showcase/director.lua, which polls a per-bot command file. Write Lua
	to that file with Send-ShowcaseCommand and the bot runs it on its next poll.

	Captures copy only the client area of the game window (no Windows frame or title bar). The
	window must be on top and the desktop visible: CopyFromScreen grabs whatever is drawn there.

	Accounts come from tools/bots/bots_provision.ps1 (prefix "showcase", password "showcasepass",
	GM level 3) against the dev login server.
#>

$ErrorActionPreference = "Stop"

$script:RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$script:RuntimeDir = Join-Path $PSScriptRoot "runtime"
$script:ClientExe = Join-Path $script:RepoRoot "bin\Debug\e2e_client.exe"

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ShowcaseWin32
{
	[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
	[StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
	[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
	[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
	[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);
	[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
	[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
	[DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
	[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
	[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
	[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
	[DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, int data, UIntPtr extra);
}
"@
[ShowcaseWin32]::SetProcessDPIAware() | Out-Null

function Get-ShowcaseWindow
{
	$process = Get-Process mmo_client -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
	if (-not $process)
	{
		throw "No mmo_client window is open."
	}
	return $process.MainWindowHandle
}

function Get-ShowcaseClientRect
{
	$hwnd = Get-ShowcaseWindow
	$rect = New-Object ShowcaseWin32+RECT
	[ShowcaseWin32]::GetClientRect($hwnd, [ref]$rect) | Out-Null
	$origin = New-Object ShowcaseWin32+POINT
	[ShowcaseWin32]::ClientToScreen($hwnd, [ref]$origin) | Out-Null
	return [pscustomobject]@{ X = $origin.X; Y = $origin.Y; Width = $rect.Right; Height = $rect.Bottom }
}

<#
.SYNOPSIS
	Brings the game window to the front. A background process cannot take focus directly, so an
	Alt tap lifts the foreground lock first.
#>
function Set-ShowcaseFocus
{
	$hwnd = Get-ShowcaseWindow
	# A lone Alt tap puts the window into menu mode, which swallows the next keystrokes, so only
	# use it when the window really has to be brought forward.
	if ([ShowcaseWin32]::GetForegroundWindow() -eq $hwnd)
	{
		return
	}

	for ($attempt = 0; $attempt -lt 3; $attempt++)
	{
		[ShowcaseWin32]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
		[ShowcaseWin32]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
		[ShowcaseWin32]::ShowWindow($hwnd, 9) | Out-Null
		[ShowcaseWin32]::BringWindowToTop($hwnd) | Out-Null
		[ShowcaseWin32]::SetForegroundWindow($hwnd) | Out-Null
		Start-Sleep -Milliseconds 400
		if ([ShowcaseWin32]::GetForegroundWindow() -eq $hwnd)
		{
			return
		}
	}
	Assert-ShowcaseFocus
}

<#
.SYNOPSIS
	Throws unless the game window is the foreground window. Every keyboard or mouse injection
	calls this first: input sent while another window is in front lands in that window.
#>
function Assert-ShowcaseFocus
{
	$hwnd = Get-ShowcaseWindow
	if ([ShowcaseWin32]::GetForegroundWindow() -ne $hwnd)
	{
		throw "The game window is not in the foreground; refusing to send input."
	}
}

<#
.SYNOPSIS
	Saves the client area of the game window (no window frame) as PNG.
#>
function Save-ShowcaseShot
{
	param([Parameter(Mandatory)][string]$Path, [switch]$NoFocus)

	if (-not $NoFocus)
	{
		Set-ShowcaseFocus
	}

	$r = Get-ShowcaseClientRect
	$bitmap = New-Object System.Drawing.Bitmap $r.Width, $r.Height
	$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
	try
	{
		$graphics.CopyFromScreen($r.X, $r.Y, 0, 0, $bitmap.Size)
		$directory = Split-Path -Parent $Path
		if ($directory -and -not (Test-Path $directory)) { New-Item -ItemType Directory -Force $directory | Out-Null }
		$bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
	}
	finally
	{
		$graphics.Dispose()
		$bitmap.Dispose()
	}
	return $Path
}

<#
.SYNOPSIS
	Moves the real cursor to a point in client coordinates. Frame UI needs to see hover across a
	few frames before a click lands, hence the wiggle.
#>
function Move-ShowcaseCursor
{
	param([int]$X, [int]$Y, [int]$HoverMs = 800)

	$r = Get-ShowcaseClientRect
	$sx = $r.X + $X
	$sy = $r.Y + $Y
	$end = (Get-Date).AddMilliseconds($HoverMs)
	$i = 0
	while ((Get-Date) -lt $end)
	{
		[ShowcaseWin32]::SetCursorPos($sx + ($i % 2), $sy) | Out-Null
		Start-Sleep -Milliseconds 50
		$i++
	}
	[ShowcaseWin32]::SetCursorPos($sx, $sy) | Out-Null
}

function Invoke-ShowcaseClick
{
	param([int]$X, [int]$Y, [switch]$Right)

	Assert-ShowcaseFocus
	Move-ShowcaseCursor -X $X -Y $Y
	if ($Right)
	{
		[ShowcaseWin32]::mouse_event(0x0008, 0, 0, 0, [UIntPtr]::Zero)
		Start-Sleep -Milliseconds 60
		[ShowcaseWin32]::mouse_event(0x0010, 0, 0, 0, [UIntPtr]::Zero)
	}
	else
	{
		[ShowcaseWin32]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
		Start-Sleep -Milliseconds 60
		[ShowcaseWin32]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
	}
	Start-Sleep -Milliseconds 300
}

<#
.SYNOPSIS
	Orbits the camera by dragging with the left mouse button (right button also turns the
	character). DeltaX/DeltaY are in client pixels, applied in small steps over DurationMs.
#>
function Invoke-ShowcaseDrag
{
	param([int]$DeltaX, [int]$DeltaY = 0, [int]$DurationMs = 600, [switch]$Right)

	Assert-ShowcaseFocus
	$r = Get-ShowcaseClientRect
	# Start on empty sky/ground above the action bar, away from UI frames.
	$sx = $r.X + [int]($r.Width / 2)
	$sy = $r.Y + [int]($r.Height * 0.3)
	[ShowcaseWin32]::SetCursorPos($sx, $sy) | Out-Null
	Start-Sleep -Milliseconds 150
	$down = if ($Right) { 0x0008 } else { 0x0002 }
	$up = if ($Right) { 0x0010 } else { 0x0004 }
	[ShowcaseWin32]::mouse_event($down, 0, 0, 0, [UIntPtr]::Zero)
	$steps = [Math]::Max(1, [int]($DurationMs / 20))
	for ($i = 1; $i -le $steps; $i++)
	{
		[ShowcaseWin32]::SetCursorPos($sx + [int]($DeltaX * $i / $steps), $sy + [int]($DeltaY * $i / $steps)) | Out-Null
		Start-Sleep -Milliseconds 20
	}
	[ShowcaseWin32]::mouse_event($up, 0, 0, 0, [UIntPtr]::Zero)
	Start-Sleep -Milliseconds 200
}

<#
.SYNOPSIS
	Scrolls the mouse wheel over the game view (negative = zoom out).
#>
function Invoke-ShowcaseWheel
{
	param([int]$Notches)

	Assert-ShowcaseFocus
	$r = Get-ShowcaseClientRect
	[ShowcaseWin32]::SetCursorPos($r.X + [int]($r.Width / 2), $r.Y + [int]($r.Height * 0.3)) | Out-Null
	$step = if ($Notches -lt 0) { -120 } else { 120 }
	for ($i = 0; $i -lt [Math]::Abs($Notches); $i++)
	{
		[ShowcaseWin32]::mouse_event(0x0800, 0, 0, $step, [UIntPtr]::Zero)
		Start-Sleep -Milliseconds 40
	}
}

<#
.SYNOPSIS
	Runs console commands in the game (GM commands need a GM account). Opens the console, types
	each line, and closes it again.
#>
function Send-ShowcaseConsole
{
	param([Parameter(Mandatory)][string[]]$Lines)

	Set-ShowcaseFocus
	Send-ShowcaseKey -Vk 0xC0
	Start-Sleep -Milliseconds 400
	foreach ($line in $Lines)
	{
		Assert-ShowcaseFocus
		[System.Windows.Forms.SendKeys]::SendWait([regex]::Replace($line, '[+^%~(){}\[\]]', '{$0}'))
		Start-Sleep -Milliseconds 150
		Send-ShowcaseKey -Vk 0x0D
		Start-Sleep -Milliseconds 500
	}
	Send-ShowcaseKey -Vk 0xC0
	Start-Sleep -Milliseconds 300
}

<#
.SYNOPSIS
	Taps a virtual key, optionally held for a while (e.g. arrow keys to turn the character).
#>
function Send-ShowcaseKey
{
	param([Parameter(Mandatory)][byte]$Vk, [int]$HoldMs = 40, [byte[]]$Modifiers = @())

	Assert-ShowcaseFocus
	foreach ($m in $Modifiers) { [ShowcaseWin32]::keybd_event($m, 0, 0, [UIntPtr]::Zero) }
	[ShowcaseWin32]::keybd_event($Vk, 0, 0, [UIntPtr]::Zero)
	Start-Sleep -Milliseconds $HoldMs
	[ShowcaseWin32]::keybd_event($Vk, 0, 2, [UIntPtr]::Zero)
	foreach ($m in $Modifiers) { [ShowcaseWin32]::keybd_event($m, 0, 2, [UIntPtr]::Zero) }
	Start-Sleep -Milliseconds 120
}

<#
.SYNOPSIS
	Types a line into the chat box (Enter, text, Enter) - slash commands like /invite work.
#>
function Send-ShowcaseChatLine
{
	param([Parameter(Mandatory)][string]$Text)

	Send-ShowcaseKey -Vk 0x0D
	Start-Sleep -Milliseconds 800
	# SendKeys treats + ^ % ~ ( ) { } [ ] specially.
	Assert-ShowcaseFocus
	$escaped = [regex]::Replace($Text, '[+^%~(){}\[\]]', '{$0}')
	[System.Windows.Forms.SendKeys]::SendWait($escaped)
	Start-Sleep -Milliseconds 200
	Send-ShowcaseKey -Vk 0x0D
	Start-Sleep -Milliseconds 300
}

<#
.SYNOPSIS
	Starts a headless bot running the director script. The character is created on first login.
#>
function Start-ShowcaseBot
{
	param(
		[Parameter(Mandatory)][int]$Index,
		[Parameter(Mandatory)][string]$Name,
		[int]$Class = 0,
		[int]$Race = 0,
		[int]$Gender = 0,
		[string]$Leader = "",
		# GUID (e.g. "0xce") of the hero the bot keeps alive during staged fights.
		[string]$Protect = "",
		[string]$Script = (Join-Path $PSScriptRoot "director.lua")
	)

	New-Item -ItemType Directory -Force $script:RuntimeDir | Out-Null
	$config = Join-Path $script:RuntimeDir "$Name.json"
	[ordered]@{
		loginHost = "127.0.0.1"
		loginPort = 3724
		realmName = "LOCALHOST"
		accountName = "showcase$Index"
		accountPassword = "showcasepass"
		characterName = $Name
		createCharacter = $true
		race = $Race
		class = $Class
		gender = $Gender
	} | ConvertTo-Json | Set-Content -Encoding ascii $config

	$commandFile = Join-Path $script:RuntimeDir "$Name.cmd.lua"
	Set-Content -Encoding ascii $commandFile ""

	$env:SHOWCASE_CMD = $commandFile
	$env:SHOWCASE_LEADER = $Leader
	$env:SHOWCASE_PROTECT = $Protect
	$process = Start-Process -FilePath $script:ClientExe -ArgumentList @(
		"--config", $config,
		"--script", $Script,
		"--character", $Name,
		"--class", "$Class",
		"--timeout", "86400"
	) -WorkingDirectory $script:RepoRoot -PassThru -WindowStyle Hidden `
		-RedirectStandardOutput (Join-Path $script:RuntimeDir "$Name.out.log") `
		-RedirectStandardError (Join-Path $script:RuntimeDir "$Name.err.log")
	return $process
}

$script:Sequence = [int](Get-Date -UFormat %s)

<#
.SYNOPSIS
	Hands a block of Lua to a running director bot. It runs on the bot's next poll (~250 ms).
#>
function Send-ShowcaseCommand
{
	param([Parameter(Mandatory)][string]$Name, [Parameter(Mandatory)][string]$Lua)

	$script:Sequence++
	$commandFile = Join-Path $script:RuntimeDir "$Name.cmd.lua"
	Set-Content -Encoding ascii $commandFile ("-- $($script:Sequence)`n" + $Lua)
}

function Get-ShowcaseBotLog
{
	param([Parameter(Mandatory)][string]$Name, [int]$Tail = 20)
	Get-Content (Join-Path $script:RuntimeDir "$Name.out.log") -Tail $Tail
}

<#
.SYNOPSIS
	Starts the default showcase party bots (accounts showcase1..4). The hero, showcase0, is
	played in the real client.
#>
function Start-ShowcaseParty
{
	param([string]$Protect = "")

	$roster = @(
		@{ Index = 1; Name = "Brannoc"; Class = 1; Gender = 0 },
		@{ Index = 2; Name = "Seraphine"; Class = 2; Gender = 1 },
		@{ Index = 3; Name = "Ysolde"; Class = 0; Gender = 1 },
		@{ Index = 4; Name = "Doran"; Class = 1; Gender = 0 })
	foreach ($bot in $roster)
	{
		Start-ShowcaseBot -Index $bot.Index -Name $bot.Name -Class $bot.Class -Gender $bot.Gender -Protect $Protect | Out-Null
		Start-Sleep -Milliseconds 800
	}
}

<#
.SYNOPSIS
	Launches the real client (auto-login via Config/RunOnce.cfg) and enters the world with the
	first character on the account.
#>
function Start-ShowcaseClient
{
	$bin = Join-Path $script:RepoRoot "bin\Debug"
	Start-Process -FilePath (Join-Path $bin "mmo_client.exe") -WorkingDirectory $bin | Out-Null
	Start-Sleep -Seconds 22
	Set-ShowcaseFocus
	$r = Get-ShowcaseClientRect
	# "Enter World" sits bottom-centre on the character screen.
	Invoke-ShowcaseClick -X ([int]($r.Width / 2)) -Y ([int]($r.Height * 0.947))
	Start-Sleep -Seconds 15
}

Export-ModuleMember -Function *-Showcase*
