# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Headless runner for the daily crash-fix loop. Invokes Claude Code non-interactively to execute the
# /fix-crash workflow (sync -> symbolicate -> analyze -> attempt fix on a branch -> document), one
# crash report per run. Intended to be launched by Windows Task Scheduler (see
# register_crash_fixer_task.ps1), but you can also run it by hand to test.
#
# Output is logged under artifacts/crash-reports/_runs/<timestamp>.log (gitignored).
#
# NOTE: this runs Claude Code with --dangerously-skip-permissions so it can edit files, run git, and
# build without prompting. That is required for unattended operation. The /fix-crash workflow is
# constrained to never push, merge, or delete server data, and to keep all changes on a review branch.

[CmdletBinding()]
param(
    # The slash command / prompt to run.
    [string]$Prompt = '/fix-crash',
    # Override the claude executable if it is not on PATH.
    [string]$Claude
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location -LiteralPath $repoRoot

if (-not $Claude) {
    $cmd = Get-Command claude -ErrorAction SilentlyContinue
    if ($cmd) { $Claude = $cmd.Source } else { throw "claude CLI not found on PATH. Pass -Claude." }
}

$runDir = Join-Path $repoRoot 'artifacts/crash-reports/_runs'
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$log = Join-Path $runDir "$stamp.log"

"=== Crash fixer run $stamp ===" | Tee-Object -FilePath $log
"Repo   : $repoRoot"            | Tee-Object -FilePath $log -Append
"Claude : $Claude"              | Tee-Object -FilePath $log -Append
"Prompt : $Prompt"              | Tee-Object -FilePath $log -Append
"" | Tee-Object -FilePath $log -Append

# Run headless. --dangerously-skip-permissions is needed for unattended file/command/git access.
& $Claude -p $Prompt --dangerously-skip-permissions --output-format text 2>&1 |
    Tee-Object -FilePath $log -Append

$code = $LASTEXITCODE
"" | Tee-Object -FilePath $log -Append
"=== Exit code: $code ===" | Tee-Object -FilePath $log -Append
exit $code
