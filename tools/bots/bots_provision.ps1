<#
.SYNOPSIS
	Creates the accounts a bot swarm logs in with.

.DESCRIPTION
	Registration goes through the login server's REST API rather than through SQL, deliberately:
	SQL would bypass exactly the realm-side validation the swarm exists to put under load, and it
	would duplicate schema knowledge in a script that has no business knowing it.

	The script is idempotent. Account names are derived from the bot index, so running it again
	over an existing population re-registers the same names and the login server rejects the
	duplicates - which is a no-op, not an error.

	Characters are NOT created here. The swarm creates them over the real CreateCharacter packet
	on first login, which is a code path worth exercising every run.

.EXAMPLE
	powershell -File tools/bots/bots_provision.ps1 -Count 20
#>
[CmdletBinding()]
param(
	[int]$Count = 10,
	[string]$Prefix = "swarm",
	[string]$Password = "swarmpass",

	# GM level the bot accounts get. Level 3 is what lets the swarm cheat bots to their rolled
	# level on login; pass 0 for a swarm that has to earn every level the hard way.
	[int]$GmLevel = 3
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $repoRoot "tools/e2e/e2e_common.psm1") -Force

$s = Get-E2eSettings
$loginRest = "http://127.0.0.1:$($s.LoginWebPort)"

Write-Host "Provisioning $Count bot accounts at $loginRest ..."

$created = 0
$existing = 0

for ($i = 0; $i -lt $Count; $i++)
{
	$account = "$Prefix$i"

	try
	{
		Invoke-RestForm -Url "$loginRest/create-account" -User $s.WebUser -Password $s.WebPassword `
			-Form @{ id = $account; password = $Password } | Out-Null
		$created++
	}
	catch
	{
		# An account that is already there is the expected outcome of a second run. Anything else
		# is worth stopping for.
		$status = $null
		if ($_.Exception.PSObject.Properties.Name -contains "Response" -and $null -ne $_.Exception.Response)
		{
			$status = [int]$_.Exception.Response.StatusCode
		}

		if ($status -eq 400 -or $status -eq 409)
		{
			$existing++
		}
		else
		{
			throw "Failed to create account ${account}: $($_.Exception.Message)"
		}
	}

	if ($GmLevel -gt 0)
	{
		Invoke-RestForm -Url "$loginRest/gm-level" -User $s.WebUser -Password $s.WebPassword `
			-Form @{ account_name = $account; gm_level = $GmLevel } | Out-Null
	}
}

Write-Host "Done: $created created, $existing already existed, GM level $GmLevel applied."
