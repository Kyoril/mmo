# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
<#
.SYNOPSIS
	Makes every .claude/skills/<name> a directory junction to the tracked .agents/skills/<name>.
.DESCRIPTION
	.agents/skills is the single tracked skill source (Codex reads it directly). Claude Code reads
	.claude/skills, which is gitignored, so it used to be a hand-maintained copy that drifted.
	This script replaces each copy with a junction. It refuses to replace a copy that differs from
	the tracked source unless -Force is given, and it never touches skills that exist only in
	.claude/skills (user-local skills). Codex-migrated command skills (source-command-*) are skipped
	because Claude Code already has those as slash commands.
	-Check only reports problems and always exits 0 (used by tools/gate/verify.ps1).
#>
param(
	[switch]$Check,
	[switch]$Force
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repoRoot ".agents\skills"
$target = Join-Path $repoRoot ".claude\skills"

function Get-NormalizedFiles([string]$root)
{
	$result = @{}
	Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object { $_.FullName -notmatch "\\__pycache__\\" } | ForEach-Object {
		$rel = $_.FullName.Substring($root.Length).TrimStart("\")
		$result[$rel] = ([IO.File]::ReadAllText($_.FullName) -replace "`r`n", "`n")
	}
	return $result
}

function Test-Junction([string]$path)
{
	$item = Get-Item -LiteralPath $path -Force
	return ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
}

$problems = @()
if (-not (Test-Path -LiteralPath $target))
{
	if ($Check)
	{
		$problems += ".claude/skills is missing (run tools/sync_skills.ps1)"
	}
	else
	{
		New-Item -ItemType Directory -Path $target | Out-Null
	}
}

foreach ($skill in Get-ChildItem -LiteralPath $source -Directory)
{
	if ($skill.Name -like "source-command-*")
	{
		continue
	}

	$link = Join-Path $target $skill.Name
	if (Test-Path -LiteralPath $link)
	{
		if (Test-Junction $link)
		{
			$resolved = (Get-Item -LiteralPath $link -Force).Target
			if ($resolved -is [array])
			{
				$resolved = $resolved[0]
			}
			if ($resolved -and ([IO.Path]::GetFullPath($resolved).TrimEnd("\") -ne $skill.FullName.TrimEnd("\")))
			{
				$problems += "$($skill.Name): junction points to $resolved instead of $($skill.FullName)"
			}
			continue
		}

		if ($Check)
		{
			$problems += "$($skill.Name): is a copy, not a junction (run tools/sync_skills.ps1)"
			continue
		}

		$a = Get-NormalizedFiles $skill.FullName
		$b = Get-NormalizedFiles $link
		$diff = @(@($a.Keys) + @($b.Keys) | Sort-Object -Unique | Where-Object { $a[$_] -ne $b[$_] })
		if ($diff.Count -gt 0 -and -not $Force)
		{
			Write-Host "REFUSING $($skill.Name): the .claude copy differs from the tracked source in:" -ForegroundColor Red
			$diff | ForEach-Object { Write-Host "  $_" }
			Write-Host "Merge those changes into .agents/skills/$($skill.Name) first, or re-run with -Force to discard them."
			exit 1
		}
		Remove-Item -LiteralPath $link -Recurse -Force
	}
	elseif ($Check)
	{
		$problems += "$($skill.Name): missing from .claude/skills (run tools/sync_skills.ps1)"
		continue
	}

	New-Item -ItemType Junction -Path $link -Target $skill.FullName | Out-Null
	Write-Host "linked $($skill.Name)"
}

if ($problems.Count -gt 0)
{
	$problems | ForEach-Object { Write-Host "WARNING: skills: $_" -ForegroundColor Yellow }
}
elseif ($Check)
{
	Write-Host "skills: .claude/skills junctions OK"
}
exit 0
