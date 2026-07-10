# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Shared helpers for the E2E test harness scripts (e2e_up.ps1 / e2e_down.ps1 / e2e_run.ps1).

Set-StrictMode -Version Latest

# Central definition of everything that distinguishes the e2e test stack from the dev stack.
function Get-E2eSettings
{
	$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

	return [ordered]@{
		RepoRoot          = $repoRoot
		RuntimeDir        = Join-Path $repoRoot "e2e\runtime"

		# Test-stack ports (chosen to never collide with the dev stack defaults).
		LoginPlayerPort   = 13724   # dev: 3724
		LoginRealmPort    = 16279   # dev: 6279
		LoginWebPort      = 18090   # dev: 8090
		LoginWebSslPort   = 18091
		RealmPlayerPort   = 18129   # dev: 8129
		RealmWorldPort    = 16280   # dev: 6280
		RealmWebPort      = 18092   # dev: 8092
		RealmWebSslPort   = 18093
		WorldWebPort      = 18094
		WorldWebSslPort   = 18095

		# Throwaway databases.
		LoginDb           = "mmo_login_e2e"
		RealmDb           = "mmo_realm_e2e"

		# REST credentials for the test stack only.
		WebUser           = "mmo-e2e"
		WebPassword       = "e2e-secret"

		# Registration identities (uppercase on purpose: hashes are sha1(UPPERNAME:UPPERPASSWORD)).
		RealmName         = "E2EREALM"
		RealmPassword     = "E2EREALMPASS"
		WorldName         = "E2EWORLD"
		WorldPassword     = "E2EWORLDPASS"

		# Test account (gets GM level 3 so scenarios can use cheat commands).
		AccountName       = "E2EGM"
		AccountPassword   = "E2EGMPASS"
	}
}

function Get-Sha1Hex([string]$Text)
{
	$sha1 = [System.Security.Cryptography.SHA1]::Create()
	try
	{
		$bytes = $sha1.ComputeHash([System.Text.Encoding]::ASCII.GetBytes($Text))
		return (($bytes | ForEach-Object { $_.ToString("x2") }) -join "")
	}
	finally
	{
		$sha1.Dispose()
	}
}

# Locates mysql.exe or aborts with an actionable message.
function Get-MysqlExe
{
	$cmd = Get-Command mysql.exe -ErrorAction SilentlyContinue
	if (-not $cmd)
	{
		# Try common install locations before giving up.
		$candidates = Get-ChildItem -Path "C:\Program Files\MySQL\*\bin\mysql.exe", "C:\Program Files\MariaDB*\bin\mysql.exe" -ErrorAction SilentlyContinue
		if ($candidates)
		{
			return $candidates[0].FullName
		}
		throw "mysql.exe not found on PATH. Install the MySQL/MariaDB client or add it to PATH."
	}
	return $cmd.Source
}

function Get-MysqlCredentials
{
	$user = $env:MMO_E2E_MYSQL_USER
	if (-not $user) { $user = "root" }
	$password = $env:MMO_E2E_MYSQL_PASSWORD
	if (-not $password)
	{
		throw "MMO_E2E_MYSQL_PASSWORD environment variable is not set. Set it to the MySQL password for user '$user' (user overridable via MMO_E2E_MYSQL_USER)."
	}
	return @{ User = $user; Password = $password }
}

# Runs a SQL string (or piped script file) through mysql.exe. Throws on non-zero exit.
function Invoke-Mysql
{
	param(
		[Parameter(Mandatory)] [string]$Sql,
		[string]$Database = ""
	)

	$mysql = Get-MysqlExe
	$cred = Get-MysqlCredentials

	$args = @("-u", $cred.User, "-h", "127.0.0.1", "--batch")
	if ($Database) { $args += @("--database=$Database") }

	$env:MYSQL_PWD = $cred.Password
	try
	{
		$output = $Sql | & $mysql @args 2>&1
		if ($LASTEXITCODE -ne 0)
		{
			throw "mysql.exe failed (exit $LASTEXITCODE): $output"
		}
		return $output
	}
	finally
	{
		Remove-Item Env:\MYSQL_PWD -ErrorAction SilentlyContinue
	}
}

# Pipes a .sql file into a database, optionally stripping USE statements so full-schema
# baselines land in the throwaway e2e database instead of the dev database they name.
function Invoke-MysqlScriptFile
{
	param(
		[Parameter(Mandatory)] [string]$Path,
		[Parameter(Mandatory)] [string]$Database,
		[switch]$StripUse
	)

	if (-not (Test-Path $Path))
	{
		throw "SQL script not found: $Path"
	}

	$sql = Get-Content $Path -Raw
	if ($StripUse)
	{
		$sql = ($sql -split "`n" | Where-Object { $_ -notmatch '^\s*USE\s' }) -join "`n"
	}
	Invoke-Mysql -Sql $sql -Database $Database | Out-Null
}

function Get-BasicAuthHeader([string]$User, [string]$Password)
{
	$token = [Convert]::ToBase64String([System.Text.Encoding]::ASCII.GetBytes("${User}:${Password}"))
	return @{ Authorization = "Basic $token" }
}

# Polls an HTTP endpoint until it answers 200 or the timeout elapses.
function Wait-HttpReady
{
	param(
		[Parameter(Mandatory)] [string]$Url,
		[Parameter(Mandatory)] [string]$User,
		[Parameter(Mandatory)] [string]$Password,
		[int]$TimeoutSec = 30,
		[string]$Description = $Url
	)

	$headers = Get-BasicAuthHeader $User $Password
	$deadline = (Get-Date).AddSeconds($TimeoutSec)
	while ((Get-Date) -lt $deadline)
	{
		try
		{
			$response = Invoke-WebRequest -Uri $Url -Headers $headers -UseBasicParsing -TimeoutSec 3
			if ($response.StatusCode -eq 200)
			{
				return
			}
		}
		catch
		{
			Start-Sleep -Milliseconds 500
		}
	}
	throw "Timed out after ${TimeoutSec}s waiting for $Description"
}

# Polls text files matching a glob until one contains the given pattern or the timeout
# elapses. Server stdout is buffered, but their file logs (logs/*.log in the server's
# working directory) flush every line, so readiness checks poll those instead.
function Wait-LogContains
{
	param(
		[Parameter(Mandatory)] [string]$PathGlob,
		[Parameter(Mandatory)] [string]$Pattern,
		[int]$TimeoutSec = 30,
		[string]$Description = $Pattern
	)

	$deadline = (Get-Date).AddSeconds($TimeoutSec)
	while ((Get-Date) -lt $deadline)
	{
		$files = Get-ChildItem -Path $PathGlob -File -ErrorAction SilentlyContinue
		foreach ($file in $files)
		{
			if (Select-String -Path $file.FullName -Pattern $Pattern -SimpleMatch -Quiet -ErrorAction SilentlyContinue)
			{
				return
			}
		}
		Start-Sleep -Milliseconds 500
	}
	throw "Timed out after ${TimeoutSec}s waiting for '$Description' in $PathGlob"
}

# Polls the login server's /realms endpoint until the named realm reports is_online.
function Wait-RealmOnline
{
	param(
		[Parameter(Mandatory)] [string]$LoginRestUrl,
		[Parameter(Mandatory)] [string]$User,
		[Parameter(Mandatory)] [string]$Password,
		[Parameter(Mandatory)] [string]$RealmName,
		[int]$TimeoutSec = 30
	)

	$headers = Get-BasicAuthHeader $User $Password
	$deadline = (Get-Date).AddSeconds($TimeoutSec)
	while ((Get-Date) -lt $deadline)
	{
		try
		{
			$response = Invoke-WebRequest -Uri "$LoginRestUrl/realms" -Headers $headers -UseBasicParsing -TimeoutSec 3
			$realms = ($response.Content | ConvertFrom-Json).realms
			foreach ($realm in $realms)
			{
				if ($realm.name -eq $RealmName -and $realm.is_online)
				{
					return
				}
			}
		}
		catch {}
		Start-Sleep -Milliseconds 500
	}
	throw "Timed out after ${TimeoutSec}s waiting for realm '$RealmName' to be online at the login server"
}

# POSTs a form to a REST endpoint with basic auth. Throws on HTTP error.
function Invoke-RestForm
{
	param(
		[Parameter(Mandatory)] [string]$Url,
		[Parameter(Mandatory)] [string]$User,
		[Parameter(Mandatory)] [string]$Password,
		[Parameter(Mandatory)] [hashtable]$Form
	)

	$headers = Get-BasicAuthHeader $User $Password
	return Invoke-WebRequest -Uri $Url -Method Post -Headers $headers -Body $Form -UseBasicParsing -TimeoutSec 10
}

# Starts a server executable detached with stdout/stderr captured to files.
function Start-E2eServer
{
	param(
		[Parameter(Mandatory)] [string]$ExePath,
		[Parameter(Mandatory)] [string[]]$Arguments,
		[Parameter(Mandatory)] [string]$WorkingDirectory,
		[Parameter(Mandatory)] [string]$StdoutPath
	)

	if (-not (Test-Path $ExePath))
	{
		throw "Server executable not found: $ExePath (build it first, e.g. cmake --build build -t <target> --config Debug)"
	}

	New-Item -ItemType Directory -Force $WorkingDirectory | Out-Null
	$stderrPath = [System.IO.Path]::ChangeExtension($StdoutPath, ".err.log")
	$process = Start-Process -FilePath $ExePath -ArgumentList $Arguments -WorkingDirectory $WorkingDirectory `
		-PassThru -WindowStyle Hidden -RedirectStandardOutput $StdoutPath -RedirectStandardError $stderrPath
	return $process
}

Export-ModuleMember -Function Get-E2eSettings, Get-Sha1Hex, Get-MysqlExe, Get-MysqlCredentials, `
	Invoke-Mysql, Invoke-MysqlScriptFile, Get-BasicAuthHeader, Wait-HttpReady, Wait-LogContains, `
	Wait-RealmOnline, Invoke-RestForm, Start-E2eServer
