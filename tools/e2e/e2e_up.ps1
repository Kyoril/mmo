# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Brings up an isolated E2E test stack (login + realm + world server) on dedicated ports
# with throwaway databases, ready for e2e_client scenario runs.
#
# Usage:
#   powershell -File tools/e2e/e2e_up.ps1 [-BuildConfig Debug|RelWithDebInfo|Release] [-HostedMaps "0"]
#
# Requirements:
#   - Servers built with -DMMO_WITH_DEV_COMMANDS=ON (GM cheat commands)
#   - mysql.exe on PATH, MMO_E2E_MYSQL_PASSWORD env var set (user via MMO_E2E_MYSQL_USER, default root)

[CmdletBinding()]
param(
	[ValidateSet("Debug", "RelWithDebInfo", "Release")]
	[string]$BuildConfig = "Debug",
	[string]$HostedMaps = "0"
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "e2e_common.psm1") -Force

$s = Get-E2eSettings
$binDir = Join-Path $s.RepoRoot "bin\$BuildConfig"
$runtime = $s.RuntimeDir
$configDir = Join-Path $runtime "config"
$logDir = Join-Path $runtime "logs"
$pidFile = Join-Path $runtime "pids.json"

# If a previous stack is still running, tear it down first (idempotent bring-up).
if (Test-Path $pidFile)
{
	Write-Host "Previous e2e stack detected - tearing it down first..."
	& (Join-Path $PSScriptRoot "e2e_down.ps1")
}

# Fail fast on missing prerequisites before touching anything.
Get-MysqlExe | Out-Null
Get-MysqlCredentials | Out-Null
foreach ($exe in @("login_server.exe", "realm_server.exe", "world_server.exe"))
{
	if (-not (Test-Path (Join-Path $binDir $exe)))
	{
		throw "$exe not found in $binDir. Build it first: cmake --build build -t $($exe -replace '\.exe$','') --config $BuildConfig"
	}
}

New-Item -ItemType Directory -Force $configDir, $logDir | Out-Null

#############################################################################################
# 1. Throwaway databases: drop, create, seed baselines. Servers self-migrate on startup
#    from data/{login,realm}/updates (updatePath in the generated configs below).
#############################################################################################

Write-Host "Resetting e2e databases ($($s.LoginDb), $($s.RealmDb))..."
Invoke-Mysql -Sql "DROP DATABASE IF EXISTS ``$($s.LoginDb)``; CREATE DATABASE ``$($s.LoginDb)``; DROP DATABASE IF EXISTS ``$($s.RealmDb)``; CREATE DATABASE ``$($s.RealmDb)``;" | Out-Null
Invoke-MysqlScriptFile -Path (Join-Path $s.RepoRoot "data\login\login_db_schema_full.sql") -Database $s.LoginDb -StripUse
Invoke-MysqlScriptFile -Path (Join-Path $s.RepoRoot "data\realm\realm_db_schema_full.sql") -Database $s.RealmDb -StripUse

#############################################################################################
# 2. Generate server configs. Registration hashes are sha1(UPPERNAME:UPPERPASSWORD) and can
#    be computed upfront because this script chooses the credentials.
#############################################################################################

$mysqlCred = Get-MysqlCredentials
$dataDir = (Join-Path $s.RepoRoot "data\editor\data") -replace '\\', '/'
$navDir = (Join-Path $s.RepoRoot "data\editor\nav") -replace '\\', '/'
$worldDataDir = (Join-Path $s.RepoRoot "data\client") -replace '\\', '/'
$scriptsDir = (Join-Path $s.RepoRoot "data\scripts") -replace '\\', '/'
$loginUpdates = (Join-Path $s.RepoRoot "data\login\updates") -replace '\\', '/'
$realmUpdates = (Join-Path $s.RepoRoot "data\realm\updates") -replace '\\', '/'

$realmHash = Get-Sha1Hex "$($s.RealmName):$($s.RealmPassword)"
$worldHash = Get-Sha1Hex "$($s.WorldName):$($s.WorldPassword)"

$loginCfg = @"
version = 1

mysqlDatabase =
(
	port = 3306
	host = "127.0.0.1"
	user = "$($mysqlCred.User)"
	password = "$($mysqlCred.Password)"
	database = "$($s.LoginDb)"
	updatePath = "$loginUpdates"
	poolSize = 1
)

webServer =
(
	port = $($s.LoginWebPort)
	ssl_port = $($s.LoginWebSslPort)
	user = "$($s.WebUser)"
	password = "$($s.WebPassword)"
)

playerManager =
(
	port = $($s.LoginPlayerPort)
	maxCount = 100
)

realmManager =
(
	port = $($s.LoginRealmPort)
	maxCount = 16
)

log =
(
	active = 1
	fileName = "logs/login"
	buffering = 0
)
"@

$realmCfg = @"
version = 1

mysqlDatabase =
(
	port = 3306
	host = "127.0.0.1"
	user = "$($mysqlCred.User)"
	password = "$($mysqlCred.Password)"
	database = "$($s.RealmDb)"
	updatePath = "$realmUpdates"
	poolSize = 1
)

realmConfig =
(
	loginServerAddress = "127.0.0.1"
	loginServerPort = $($s.LoginRealmPort)
	realmName = "$($s.RealmName)"
	realmPasswordHash = "$realmHash"
)

webServer =
(
	port = $($s.RealmWebPort)
	ssl_port = $($s.RealmWebSslPort)
	user = "$($s.WebUser)"
	password = "$($s.WebPassword)"
)

playerManager =
(
	port = $($s.RealmPlayerPort)
	maxCount = 20
)

worldManager =
(
	port = $($s.RealmWorldPort)
	maxCount = 16
)

folders =
(
	data = "$dataDir"
)

log =
(
	active = 1
	fileName = "logs/realm"
	buffering = 0
)
"@

# Note: the world server does not use its database yet (program.cpp DB setup is TODO);
# the section is kept for forward compatibility but no mmo_world_e2e database is created.
$worldCfg = @"
version = 1

mysqlDatabase =
(
	port = 3306
	host = "127.0.0.1"
	user = "$($mysqlCred.User)"
	password = "$($mysqlCred.Password)"
	database = "mmo_world_e2e"
)

worldConfig =
(
	realmServerAddress = "127.0.0.1"
	realmServerPort = $($s.RealmWorldPort)
	realmServerAuthName = "$($s.WorldName)"
	realmServerPassword = "$worldHash"
	hostedMaps = {$HostedMaps}
)

webServer =
(
	port = $($s.WorldWebPort)
	ssl_port = $($s.WorldWebSslPort)
	user = "$($s.WebUser)"
	password = "$($s.WebPassword)"
)

playerManager =
(
	maxCount = 100
)

folders =
(
	data = "$dataDir"
	maps = "$navDir"
	worldData = "$worldDataDir"
	scripts = "$scriptsDir"
)

log =
(
	active = 1
	fileName = "logs/world"
	buffering = 0
)
"@

Set-Content -Path (Join-Path $configDir "login_server.cfg") -Value $loginCfg -Encoding UTF8
Set-Content -Path (Join-Path $configDir "realm_server.cfg") -Value $realmCfg -Encoding UTF8
Set-Content -Path (Join-Path $configDir "world_server.cfg") -Value $worldCfg -Encoding UTF8

#############################################################################################
# 3. Launch and register, strictly sequenced with readiness checks.
#############################################################################################

$pids = [ordered]@{}

# The pid file is rewritten after each server start so that e2e_down.ps1 can clean up
# even if a later bring-up step fails.
function Save-Pids
{
	$pids | ConvertTo-Json | Set-Content -Path $pidFile -Encoding UTF8
}

Write-Host "Starting login server..."
$loginOut = Join-Path $logDir "login_server.out.log"
$login = Start-E2eServer -ExePath (Join-Path $binDir "login_server.exe") `
	-Arguments @("-c", (Join-Path $configDir "login_server.cfg")) `
	-WorkingDirectory (Join-Path $runtime "login") -StdoutPath $loginOut
$pids.login = $login.Id
Save-Pids
Wait-HttpReady -Url "http://127.0.0.1:$($s.LoginWebPort)/uptime" -User $s.WebUser -Password $s.WebPassword `
	-TimeoutSec 30 -Description "login server REST"

Write-Host "Registering realm, test account and GM level at login server..."
$loginRest = "http://127.0.0.1:$($s.LoginWebPort)"
Invoke-RestForm -Url "$loginRest/create-realm" -User $s.WebUser -Password $s.WebPassword `
	-Form @{ id = $s.RealmName; password = $s.RealmPassword; address = "127.0.0.1"; port = $s.RealmPlayerPort } | Out-Null
Invoke-RestForm -Url "$loginRest/create-account" -User $s.WebUser -Password $s.WebPassword `
	-Form @{ id = $s.AccountName; password = $s.AccountPassword } | Out-Null
Invoke-RestForm -Url "$loginRest/gm-level" -User $s.WebUser -Password $s.WebPassword `
	-Form @{ account_name = $s.AccountName; gm_level = 3 } | Out-Null

Write-Host "Starting realm server..."
$realmOut = Join-Path $logDir "realm_server.out.log"
$realm = Start-E2eServer -ExePath (Join-Path $binDir "realm_server.exe") `
	-Arguments @("-c", (Join-Path $configDir "realm_server.cfg")) `
	-WorkingDirectory (Join-Path $runtime "realm") -StdoutPath $realmOut
$pids.realm = $realm.Id
Save-Pids
Wait-HttpReady -Url "http://127.0.0.1:$($s.RealmWebPort)/uptime" -User $s.WebUser -Password $s.WebPassword `
	-TimeoutSec 30 -Description "realm server REST"
Wait-RealmOnline -LoginRestUrl $loginRest -User $s.WebUser -Password $s.WebPassword `
	-RealmName $s.RealmName -TimeoutSec 30

Write-Host "Registering world node at realm server..."
Invoke-RestForm -Url "http://127.0.0.1:$($s.RealmWebPort)/create-world" -User $s.WebUser -Password $s.WebPassword `
	-Form @{ id = $s.WorldName; password = $s.WorldPassword } | Out-Null

Write-Host "Starting world server..."
$worldOut = Join-Path $logDir "world_server.out.log"
$world = Start-E2eServer -ExePath (Join-Path $binDir "world_server.exe") `
	-Arguments @("-c", (Join-Path $configDir "world_server.cfg")) `
	-WorkingDirectory (Join-Path $runtime "world") -StdoutPath $worldOut
$pids.world = $world.Id
Save-Pids
# The world server has no REST service yet; its file log (flushed per line) is the only
# reliable readiness signal.
Wait-LogContains -PathGlob (Join-Path $runtime "world\logs\*.log") -Pattern "Successfully authenticated at the realm server" `
	-TimeoutSec 60 -Description "world-to-realm authentication"

#############################################################################################
# 4. Persist stack state for the e2e_client.
#############################################################################################

# Connection info consumed by e2e_client --config (phase 2+ of the harness).
[ordered]@{
	loginHost       = "127.0.0.1"
	loginPort       = $s.LoginPlayerPort
	realmName       = $s.RealmName
	accountName     = $s.AccountName
	accountPassword = $s.AccountPassword
	characterName   = "Smoke"  # letters only, 3-12 chars (realm-side validation)
	createCharacter = $true
	race            = 0
	class           = 0        # Mage - has mana, so scenarios can cast spells
	gender          = 0
} | ConvertTo-Json | Set-Content -Path (Join-Path $runtime "e2e_client.json") -Encoding UTF8

Write-Host ""
Write-Host "E2E stack is up:"
Write-Host "  login  : pid $($pids.login)  (auth $($s.LoginPlayerPort), REST $($s.LoginWebPort))"
Write-Host "  realm  : pid $($pids.realm)  (game $($s.RealmPlayerPort), REST $($s.RealmWebPort))"
Write-Host "  world  : pid $($pids.world)"
Write-Host "  account: $($s.AccountName) (GM 3)"
Write-Host "  logs   : $logDir"
Write-Host "Tear down with: powershell -File tools/e2e/e2e_down.ps1"
