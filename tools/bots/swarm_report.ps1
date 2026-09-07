<#
.SYNOPSIS
	Turns a swarm run's JSONL trace into a pass/fail report under tools/gate/reports/.

.DESCRIPTION
	The report is shaped like the gate's own last_report.json - branch, commit, timestamp,
	steps[] - so the "is the newest report in tools/gate/reports red?" convention in CLAUDE.md
	covers the swarm for free, with no new tooling to read it.

	What makes a swarm run pass is not "it finished". A run where every bot logs in and then
	stands still is a green process and a red game, so the thresholds are about whether the bots
	actually played: did they get into the world, did they kill things, did they stay connected,
	and did the server refuse to believe in their movement.

.EXAMPLE
	powershell -File tools/bots/swarm_report.ps1 -Jsonl e2e/runtime/logs/swarm.jsonl -DurationSeconds 600
#>
[CmdletBinding()]
param(
	[string]$Jsonl = "e2e/runtime/logs/swarm.jsonl",

	# Wall-clock length of the run, used for the per-bot-minute rates.
	[Parameter(Mandatory)] [int]$DurationSeconds,

	# A bot that never reaches the world tests nothing at all.
	[double]$MinEnteredWorldFraction = 0.9,

	# The headline measure: bots that do not kill anything are not playing the game.
	[double]$MinKillsPerBotMinute = 1.0,

	# Being wedged is the failure mode that hides behind a green run.
	[double]$MaxStuckPerBotHour = 6.0,

	[string]$WorldLog = "e2e/runtime/logs/world_server.out.log",
	[string]$OutputDir = "tools/gate/reports"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $repoRoot

if (-not (Test-Path $Jsonl))
{
	throw "Swarm telemetry not found: $Jsonl"
}

# Parsed line by line so that a corrupt trace names the line it went wrong on. That happens for
# a real reason worth surfacing rather than tolerating: two swarms pointed at the same telemetry
# path interleave and truncate each other, and the counters from such a file are a blend of two
# runs, which would be reported as one perfectly plausible result.
$events = @()
$lineNumber = 0
foreach ($line in Get-Content $Jsonl)
{
    $lineNumber++
    if (-not $line) { continue }

    try
    {
        $events += ($line | ConvertFrom-Json)
    }
    catch
    {
        throw "Malformed telemetry at ${Jsonl}:${lineNumber}. Two swarm runs writing the same --jsonl path will do this; give each run its own file."
    }
}
$summary = $events | Where-Object { $_.type -eq "summary" } | Select-Object -Last 1

if (-not $summary)
{
	# No summary line means the swarm did not shut down cleanly. That is itself the finding, and
	# it must not be reported as a pass just because the counters happen to be readable.
	throw "No summary event in $Jsonl - the run did not finish cleanly"
}

$bots = [double]$summary.entered_world
$minutes = [double]$DurationSeconds / 60.0
$hours = [double]$DurationSeconds / 3600.0

$killsPerBotMinute = if ($bots -gt 0 -and $minutes -gt 0) { $summary.kills / $bots / $minutes } else { 0 }
$stuckPerBotHour = if ($bots -gt 0 -and $hours -gt 0) { $summary.stuck_events / $bots / $hours } else { 0 }
$enteredFraction = if ($summary.bots_configured -gt 0) { $bots / $summary.bots_configured } else { 0 }

# The world server has no REST endpoint to ask, so its log is the only place an anti-cheat kick
# shows up. A swarm whose movement the server refuses to believe is the single most important
# thing this report can catch, because every other counter still looks healthy while it happens.
$antiCheatLines = 0
if (Test-Path $WorldLog)
{
	$antiCheatLines = @(Select-String -Path $WorldLog -Pattern "\[AntiCheat\]" -SimpleMatch:$false -ErrorAction SilentlyContinue).Count
}

function New-Step
{
	param([string]$Name, [bool]$Passed, [string]$Detail)
	return [ordered]@{ name = $Name; passed = $Passed; detail = $Detail }
}

$steps = @(
	New-Step "entered_world" ($enteredFraction -ge $MinEnteredWorldFraction) `
		("$($summary.entered_world)/$($summary.bots_configured) bots entered the world (need $([math]::Round($MinEnteredWorldFraction * 100))%)")
	New-Step "kill_rate" ($killsPerBotMinute -ge $MinKillsPerBotMinute) `
		("$([math]::Round($killsPerBotMinute, 2)) kills per bot-minute (need $MinKillsPerBotMinute)")
	New-Step "no_unexpected_disconnects" ($summary.disconnects -eq 0) `
		("$($summary.disconnects) disconnects")
	New-Step "no_anticheat_kicks" ($antiCheatLines -eq 0) `
		("$antiCheatLines anti-cheat log lines in $WorldLog")
	New-Step "stuck_rate" ($stuckPerBotHour -le $MaxStuckPerBotHour) `
		("$([math]::Round($stuckPerBotHour, 2)) stuck events per bot-hour (max $MaxStuckPerBotHour)")
	New-Step "no_errors" ($summary.errors -eq 0) `
		("$($summary.errors) bot errors")
)

$report = [ordered]@{
	branch = (& git rev-parse --abbrev-ref HEAD).Trim()
	commit = (& git rev-parse HEAD).Trim()
	timestamp = (Get-Date).ToString("o")
	kind = "swarm"
	duration_s = $DurationSeconds
	passed = -not ($steps | Where-Object { -not $_.passed })
	steps = $steps
	swarm = [ordered]@{
		bots_configured = $summary.bots_configured
		entered_world = $summary.entered_world
		peak_online = $summary.peak_online
		kills = $summary.kills
		kills_per_bot_minute = [math]::Round($killsPerBotMinute, 3)
		deaths = $summary.deaths
		level_ups = $summary.level_ups
		swings = $summary.swings
		damage_dealt = $summary.damage_dealt
		stuck_events = $summary.stuck_events
		stuck_per_bot_hour = [math]::Round($stuckPerBotHour, 3)
		disconnects = $summary.disconnects
		errors = $summary.errors
		anticheat_log_lines = $antiCheatLines
		path_failures = $summary.path_failures
		swing_errors = $summary.swing_errors
		actions = $summary.actions
	}
}

New-Item -ItemType Directory -Force $OutputDir | Out-Null
$outPath = Join-Path $OutputDir ("swarm-" + (Get-Date -Format "yyyy-MM-dd-HHmmss") + ".json")
$report | ConvertTo-Json -Depth 6 | Set-Content -Path $outPath -Encoding UTF8

foreach ($step in $steps)
{
	$mark = if ($step.passed) { "PASS" } else { "FAIL" }
	Write-Host ("  {0}  {1} - {2}" -f $mark, $step.name, $step.detail)
}

Write-Host ""
Write-Host ("Swarm run {0}: {1}" -f $(if ($report.passed) { "GREEN" } else { "RED" }), $outPath)

exit $(if ($report.passed) { 0 } else { 1 })
