# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Local quality gate: protocol version check -> build -> unit tests -> tool tests -> Linux build and tests (WSL, Windows
# only) -> E2E. Writes tools/gate/last_report.json and exits 0 only if every step passed.
#
# Tiers:
#   fast  protocol check, build, unit tests, tool tests, Linux check. What /ship requires. Runs only the
#         steps the change can affect (tools/gate/select_steps.py): a Python tool change skips the
#         C++ builds, a documentation change runs the protocol check alone. -All runs every step.
#   full  fast + E2E (~8.5 min). Run by the GitHub nightly (on Linux), /gate full and release_check.ps1.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -Tier fast
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1            # full
#
# $env:MMO_GATE_PYTHON overrides the python executable (the WindowsApps "python" alias is not
# launchable from every context, e.g. Claude sessions or Task Scheduler).

[CmdletBinding()]
param(
	[ValidateSet("fast", "full")]
	[string]$Tier = "full",
	# Legacy spelling of -Tier fast.
	[switch]$SkipE2E,
	# all_tests is an aggregate that every mmo_add_test() suite adds itself to, so a new
	# test suite never needs an edit here.
	[string[]]$Targets = @("login_server", "realm_server", "world_server", "e2e_client", "all_tests"),
	# The fast tier without the selection by changed files: every step runs.
	[switch]$All
)

if ($SkipE2E)
{
	$Tier = "fast"
}

$python = if ($env:MMO_GATE_PYTHON) { $env:MMO_GATE_PYTHON } else { "python" }
$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
# Windows PowerShell 5.1 has no $IsWindows; it only runs on Windows.
$onWindows = ($PSVersionTable.PSEdition -eq "Desktop") -or [bool]$IsWindows
$logDir = Join-Path $PSScriptRoot "logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

$steps = @()
$allPassed = $true
$gateCompleted = $false

function Invoke-GateStep
{
	param(
		[string]$Name,
		[string]$Exe,
		[string[]]$Arguments = @(),
		# ctest has to run from the build directory. Its --test-dir flag would avoid this
		# but needs CMake 3.20, and the project floor is 3.12.
		[string]$WorkingDirectory
	)

	$log = Join-Path $script:logDir ($Name + ".log")
	Write-Host ("== {0} ==" -f $Name)
	$sw = [System.Diagnostics.Stopwatch]::StartNew()

	try
	{
		# Pass/fail must rest on exit codes, not stderr chatter, so native invocation runs
		# under 'Continue'. Scoped narrowly and restored immediately after.
		$previousEap = $ErrorActionPreference
		$ErrorActionPreference = "Continue"
		$pushed = $false
		try
		{
			if ($WorkingDirectory)
			{
				Push-Location $WorkingDirectory -ErrorAction Stop
				$pushed = $true
			}

			# Out-Host keeps the command output off the pipeline so the function returns ONLY the boolean.
			& $Exe @Arguments 2>&1 | Tee-Object -FilePath $log | Out-Host
		}
		finally
		{
			if ($pushed)
			{
				Pop-Location
			}
			$ErrorActionPreference = $previousEap
		}
		$exit = $LASTEXITCODE
		$sw.Stop()

		$script:steps += [ordered]@{
			name = $Name
			passed = ($exit -eq 0)
			exit_code = $exit
			log = ("tools/gate/logs/{0}.log" -f $Name)
			duration_s = [math]::Round($sw.Elapsed.TotalSeconds, 1)
		}
		if ($exit -ne 0)
		{
			$script:allPassed = $false
		}
		return ($exit -eq 0)
	}
	catch
	{
		$sw.Stop()
		Add-Content -Path $log -Value ("EXCEPTION: {0}" -f $_.Exception.Message)

		$script:steps += [ordered]@{
			name = $Name
			passed = $false
			exit_code = -1
			log = ("tools/gate/logs/{0}.log" -f $Name)
			duration_s = [math]::Round($sw.Elapsed.TotalSeconds, 1)
		}
		$script:allPassed = $false
		return $false
	}
}

$branch = (& git -C $repoRoot rev-parse --abbrev-ref HEAD)
$commit = (& git -C $repoRoot rev-parse HEAD)
# A report from a dirty tree describes more than the commit it names; /ship refuses those.
$dirty = [bool](& git -C $repoRoot status --porcelain)

Push-Location $repoRoot
try
{
	# Which steps the change needs. The full tier always runs everything; so does a selection
	# that fails (git unreachable, a broken script): it never skips on doubt.
	$selection = [pscustomobject]@{ cpp = $true; tool_tests = "all"; deployer = $true; reason = "every step runs" }
	if ($Tier -eq "fast" -and -not $All)
	{
		try
		{
			$selected = (& $python (Join-Path $repoRoot "tools/gate/select_steps.py") --base origin/develop) | ConvertFrom-Json
			if ($LASTEXITCODE -eq 0 -and $selected) { $selection = $selected }
		}
		catch
		{
			Write-Host ("change selection failed, every step runs: {0}" -f $_) -ForegroundColor Yellow
		}
	}
	Write-Host ("== selection: {0}" -f $selection.reason)

	# First, and before the build, because it costs milliseconds and answers a question the
	# build cannot: did the wire format change without the version bump that makes incompatible
	# peers get rejected instead of silently misparsing each other?
	$ok = Invoke-GateStep -Name "protocol" -Exe $python -Arguments @("tools/protocol_version_check.py")

	# After the check, not before: when the manifest is simply stale the step above says so
	# in one clear line, and that is the common case. These tests are here for the case that
	# check cannot report on -- the checker itself quietly breaking and passing everything.
	if ($ok)
	{
		$ok = Invoke-GateStep -Name "protocol_tests" -Exe $python -Arguments @("tools/tests/test_protocol_version_check.py")
	}

	# Warning only: a stale .claude/skills copy does not break the build, but it is how an agent
	# ends up following instructions the tracked .agents/skills no longer contains.
	if ($onWindows)
	{
		& (Join-Path $repoRoot "tools/sync_skills.ps1") -Check
	}

	if ($ok -and $selection.cpp)
	{
		$ok = Invoke-GateStep -Name "build" -Exe "cmake" -Arguments (@("--build", "build", "--config", "Debug", "-t") + $Targets)
	}

	if ($ok -and $selection.cpp)
	{
		# One ctest run covers every suite mmo_add_test() registered, so a new suite needs no
		# edit here. --output-on-failure names the failing suite and prints its output into
		# tools/gate/logs/tests.log.
		# Not --parallel: game_server_tests alone is most of the step, and its real-time tests
		# (aura ticks, pet timers) failed on time under the extra load on 2026-11-26.
		$ok = Invoke-GateStep -Name "tests" -Exe "ctest" -Arguments @("-C", "Debug", "--output-on-failure") -WorkingDirectory (Join-Path $repoRoot "build")
	}

	# Discovery rather than a list of filenames: a tool test that nobody runs rots silently,
	# and the one thing worse than an untested tool is a tool with tests that stopped being
	# true. This re-runs the protocol tests above as part of the sweep, which costs
	# milliseconds and keeps the step free of exclusions. Named separately so a failure here
	# does not read as the protocol checker itself being broken. Runs after the build because
	# some tool tests need the protoc built by it.
	if ($ok -and $selection.tool_tests -ne "none")
	{
		# In parallel processes (tools/gate/run_tool_tests.py): run one after the other in a single
		# process the tool tests took 37 minutes on 2026-11-26, nearly all of it waiting on git
		# and PowerShell subprocesses.
		$toolArgs = @("tools/gate/run_tool_tests.py", "--start-dir", "tools/tests")
		if ($selection.tool_tests -ne "all")
		{
			$toolArgs += @("--modules", (@($selection.tool_tests) -join ","))
		}
		$ok = Invoke-GateStep -Name "tool_tests" -Exe $python -Arguments $toolArgs
	}

	# The deployer ships to the live server; its tests gate every merge that can touch it.
	if ($ok -and $selection.deployer)
	{
		$ok = Invoke-GateStep -Name "deployer_tests" -Exe $python -Arguments @("-m", "unittest", "discover", "-s", "deploy/deployer/tests", "-t", "deploy/deployer", "-p", "test_*.py")
	}

	# The servers ship on Linux, and MSVC is not gcc: a double free libstdc++ aborts on, or a
	# test that only runs where symlinks work, passes here and turns the nightly red. On Windows
	# the same targets therefore build and test once more in WSL (Ubuntu 24.04, like CI); on
	# Linux the steps above already were that check.
	if ($ok -and $selection.cpp -and $onWindows -and -not (Get-Command wsl.exe -ErrorAction SilentlyContinue))
	{
		Write-Host "WSL is not installed - the Linux check cannot run (see tools/gate/linux_gate.sh)." -ForegroundColor Red
		$steps += [ordered]@{ name = "linux"; passed = $false; exit_code = -1; log = $null; duration_s = 0 }
		$allPassed = $false
		$ok = $false
	}
	elseif ($ok -and $selection.cpp -and $onWindows)
	{
		$checkout = (& wsl.exe -e wslpath -a ($repoRoot -replace '\\', '/'))
		$script = "$checkout/tools/gate/linux_gate.sh"
		$ok = Invoke-GateStep -Name "linux" -Exe "wsl.exe" -Arguments (@("-e", "bash", $script, $checkout) + $Targets)
	}

	if ($ok -and $Tier -eq "full")
	{
		if (-not $env:MMO_E2E_MYSQL_PASSWORD)
		{
			Write-Host "MMO_E2E_MYSQL_PASSWORD is not set - cannot run E2E." -ForegroundColor Red
			$steps += [ordered]@{ name = "e2e"; passed = $false; exit_code = -1; log = $null; duration_s = 0 }
			$allPassed = $false
		}
		else
		{
			$shellExe = (Get-Process -Id $PID).Path
			$null = Invoke-GateStep -Name "e2e" -Exe $shellExe -Arguments @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $repoRoot "tools/e2e/e2e_run.ps1"))
		}
	}

	$gateCompleted = $true
}
finally
{
	Pop-Location

	# Belt-and-suspenders: any crash that skips the rest of the try block leaves
	# $gateCompleted false, so the report is red even if $allPassed never flipped.
	$reportPassed = ($allPassed -and $gateCompleted)

	[ordered]@{
		branch = $branch
		commit = $commit
		dirty = $dirty
		targets = $Targets
		timestamp = (Get-Date -Format "o")
		config = "Debug"
		tier = $Tier
		e2e_skipped = ($Tier -ne "full")
		# What the change needed (fast tier): the steps not listed in `steps` were not affected.
		selection = $(if ($selection) { $selection.reason } else { "every step runs" })
		steps = $steps
		passed = $reportPassed
	} | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $PSScriptRoot "last_report.json") -Encoding UTF8
}

Write-Host ""
Write-Host ("Gate result: {0} (report: tools/gate/last_report.json)" -f $(if ($reportPassed) { "GREEN" } else { "RED" }))
exit $(if ($reportPassed) { 0 } else { 1 })
