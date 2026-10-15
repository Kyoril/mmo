# Client Setup (Installer) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship `AlestiaOnline-Setup.exe` with every `launcher-v*` release: a small Inno Setup installer that lets the player pick an install folder, installs only `Launcher.exe`, and uninstalls the game cleanly.

**Architecture:** One Inno Setup script (`deploy/installer/setup.iss`) built by a PowerShell wrapper (`build_setup.ps1`) that CI and developers share. A second script (`smoke_test.ps1`) installs and uninstalls silently and asserts the result; CI runs it before publishing. A Python tool test keeps the uninstall list in sync with the patch manifest. No launcher code changes.

**Tech Stack:** Inno Setup 6 (ISCC), PowerShell 7 (`pwsh`), GitHub Actions (`windows-latest`), Python stdlib `unittest` (tool tests).

**Spec:** `docs/superpowers/specs/2026-10-15-client-setup-design.md`

## Global Constraints

- Branch: `feature/client-setup` (exists, spec committed). Never push; land with `/ship`.
- `AppId={{032178BC-35CD-47F5-BA65-309AF9F74D43}` — fixed forever, never regenerate.
- `AppName=Alestia Online`, `AppPublisher=Kyoril`, `OutputBaseFilename=AlestiaOnline-Setup`.
- `DefaultDirName={autopf}\Alestia Online`; `PrivilegesRequired=lowest`; per-user is the default.
- The installed launcher is always named `Launcher.exe`.
- Uninstall never deletes `{app}` recursively; only the listed launcher/client files and folders, then `{app}` if empty.
- Languages: English and German.
- Version comes only from the `launcher-vX.Y.Z` tag (passed as `/DAppVersion`); nothing hardcoded.
- Stable download URL: `https://github.com/Kyoril/mmo/releases/latest/download/AlestiaOnline-Setup.exe`; every non-`launcher-v*` release must stay a prerelease.
- Copyright header on new source files: `Copyright (C) 2019 - 2025, Kyoril. All rights reserved.` (`;` comment in `.iss`, `#` in `.ps1`/`.py`). Tabs for indentation.
- Python tests are stdlib `unittest`, live in `tools/tests/test_*.py`, and are picked up by the gate's `tool_tests` step automatically.

## Review Focus

1. **Install folder with spaces and non-ASCII characters** (e.g. `C:\Users\Jörg\...`, `Alestia Online`) — install, shortcut and uninstall must work. Pinned: smoke test installs into a temp folder named `Alestia Önline Smoke` (Task 2).
2. **Installing into an existing, non-empty folder** (e.g. `C:\Games`) — uninstall must leave unrelated files and the folder itself. Pinned: smoke test scenario "foreign file survives" (Task 2).
3. **Running a newer setup over an existing install** — downloaded game data must survive, only `Launcher.exe` is replaced. Pinned: smoke test scenario "upgrade keeps data" (Task 2).
4. **All-users install** — the launcher must be able to write into `Program Files\Alestia Online` without elevation. Pinned: smoke test `-AllUsers` checks the Users group has Modify on `{app}` (Task 2, run in CI where the runner is admin).
5. **Patch manifest grows a new top-level file** — uninstall would silently leave it behind. Pinned: `test_installer_uninstall_list.py` fails until `setup.iss` lists it (Task 1).

---

## File Structure

| File | Responsibility |
|---|---|
| `deploy/installer/setup.iss` (create) | The installer: install model, files, shortcuts, uninstall list. |
| `deploy/installer/build_setup.ps1` (create) | Find ISCC, compile `setup.iss` for a version + launcher binary, print output path. |
| `deploy/installer/smoke_test.ps1` (create) | Silent install/upgrade/uninstall scenarios with assertions; exit 1 on failure. |
| `deploy/installer/README.md` (create) | How to build and smoke-test locally. |
| `tools/tests/test_installer_uninstall_list.py` (create) | Every top-level patch entry is removed by uninstall. |
| `deploy/patch/source.txt` (modify, top) | `//` comment pointing at `setup.iss`. |
| `.github/workflows/launcher-release.yml` (modify) | Install Inno, build setup, smoke test, publish asset. |
| `docs/versioning.md` (modify, "Launcher releases") | Release now carries the setup. |
| `docs/deployment.md` (modify, §2 near line 53, §7) | Website link + prerelease invariant + uninstall-list note. |

---

### Task 1: Installer script and uninstall-list guard

**Files:**
- Create: `tools/tests/test_installer_uninstall_list.py`
- Create: `deploy/installer/setup.iss`
- Modify: `deploy/patch/source.txt:1`

**Interfaces:**
- Consumes: `tools/release/package_client.py` → `binaries_from_source(text) -> list[str]` (sorted top-level `.exe`/`.dll` names from `source.txt`).
- Produces: `deploy/installer/setup.iss` accepting ISCC defines `AppVersion` (e.g. `1.0.1`) and `LauncherExe` (absolute path to the launcher binary). Output file `AlestiaOnline-Setup.exe` in the directory given by ISCC's `/O`.

- [ ] **Step 1: Write the failing test**

Create `tools/tests/test_installer_uninstall_list.py`:

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Keeps the setup's uninstall list in sync with the patch manifest.

The launcher downloads every top-level entry of deploy/patch/source.txt into the install
folder. Uninstall must remove each of them, but it may never delete the folder recursively
(a player may have installed into C:\\Games). So setup.iss lists them one by one, and this
test fails when the manifest gains a top-level entry the list does not cover.

    python tools/tests/test_installer_uninstall_list.py
"""

import importlib.util
import os
import re
import sys
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SOURCE_TXT = os.path.join(REPO_ROOT, "deploy", "patch", "source.txt")
SETUP_ISS = os.path.join(REPO_ROOT, "deploy", "installer", "setup.iss")


def _load_package_client():
	path = os.path.join(REPO_ROOT, "tools", "release", "package_client.py")
	spec = importlib.util.spec_from_file_location("package_client", path)
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


# A top-level directory entry sits one tab deep under root: (type = "fs", ..., to = "Data", entries =
_TOP_LEVEL_DIR = re.compile(r'^\t\(type = "fs", from = "[^"]*", to = "([^"/\\]+)", entries', re.MULTILINE)
# An [UninstallDelete] line: Type: files|filesandordirs; Name: "{app}\<name>"
_UNINSTALL_ENTRY = re.compile(r'^Type:\s*(files|filesandordirs);\s*Name:\s*"\{app\}\\([^"]+)"', re.MULTILINE)


def top_level_entries(source_text):
	binaries = _load_package_client().binaries_from_source(source_text)
	directories = _TOP_LEVEL_DIR.findall(source_text)
	return sorted(set(binaries) | set(directories))


def uninstall_names(iss_text):
	section = iss_text.split("[UninstallDelete]", 1)[1].split("\n[", 1)[0]
	return {name for _, name in _UNINSTALL_ENTRY.findall(section)}


class UninstallList(unittest.TestCase):
	def setUp(self):
		with open(SOURCE_TXT, encoding="utf-8") as handle:
			self.source = handle.read()
		with open(SETUP_ISS, encoding="utf-8-sig") as handle:
			self.iss = handle.read()

	def test_manifest_parser_sees_the_known_entries(self):
		# Guards the regexes: if they silently matched nothing, the sync test would pass vacuously.
		entries = top_level_entries(self.source)
		for expected in ("Launcher.exe", "mmo_client.exe", "fmod.dll", "Data"):
			self.assertIn(expected, entries)

	def test_every_downloaded_entry_is_removed_on_uninstall(self):
		listed = uninstall_names(self.iss)
		# Launcher.exe is installed by [Files], so Inno removes it on its own.
		missing = [name for name in top_level_entries(self.source) if name != "Launcher.exe" and name not in listed]
		self.assertEqual([], missing, "add these to [UninstallDelete] in deploy/installer/setup.iss")

	def test_client_runtime_folders_are_removed(self):
		listed = uninstall_names(self.iss)
		for folder in ("Logs", "Config", "Cache"):
			self.assertIn(folder, listed)

	def test_install_folder_is_never_deleted_recursively(self):
		self.assertNotRegex(self.iss, r'(?m)^Type:\s*filesandordirs;\s*Name:\s*"\{app\}"')
		self.assertRegex(self.iss, r'(?m)^Type:\s*dirifempty;\s*Name:\s*"\{app\}"')


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python tools/tests/test_installer_uninstall_list.py`
Expected: ERROR in `setUp` — `FileNotFoundError` for `deploy/installer/setup.iss`.

- [ ] **Step 3: Write `deploy/installer/setup.iss`**

```ini
; Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
;
; Alestia Online setup: installs Launcher.exe into a folder the player picks; the launcher then
; downloads the game there. Design: docs/superpowers/specs/2026-10-15-client-setup-design.md
;
; Build with deploy/installer/build_setup.ps1, or directly:
;   iscc /DAppVersion=1.0.1 /DLauncherExe=C:\path\Launcher.exe /Odist deploy\installer\setup.iss

#ifndef AppVersion
  #error Define AppVersion, e.g. iscc /DAppVersion=1.0.1
#endif
#ifndef LauncherExe
  #error Define LauncherExe, the absolute path of the Launcher.exe to package
#endif

[Setup]
; Never change AppId: it is how a newer setup finds an existing install.
AppId={{032178BC-35CD-47F5-BA65-309AF9F74D43}
AppName=Alestia Online
AppVersion={#AppVersion}
AppVerName=Alestia Online
AppPublisher=Kyoril
AppPublisherURL=https://mmo-dev.net
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\Alestia Online
DisableDirPage=no
DisableProgramGroupPage=yes
; Per-user by default (no UAC). The dialog offers "all users", which installs to Program Files.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=AlestiaOnline-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Alestia Online
UninstallDisplayIcon={app}\Launcher.exe

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Dirs]
; The launcher patches the game in place without elevation, so an all-users install in
; Program Files needs the folder writable for Users. Harmless for per-user installs.
Name: "{app}"; Permissions: users-modify

[Files]
Source: "{#LauncherExe}"; DestDir: "{app}"; DestName: "Launcher.exe"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Alestia Online"; Filename: "{app}\Launcher.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Alestia Online"; Filename: "{app}\Launcher.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\Launcher.exe"; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,Alestia Online}"; Flags: postinstall nowait skipifsilent runasoriginaluser

[UninstallDelete]
; Only what the launcher downloads (deploy/patch/source.txt, top level) and what the client
; writes into its working directory. Never {app} recursively: the player may have picked a
; folder with other files in it. tools/tests/test_installer_uninstall_list.py keeps this in sync.
Type: filesandordirs; Name: "{app}\Data"
Type: files; Name: "{app}\mmo_client.exe"
Type: files; Name: "{app}\mmo_error.exe"
Type: files; Name: "{app}\fmod.dll"
Type: files; Name: "{app}\launcher.ini"
Type: files; Name: "{app}\Launcher.exe.*"
Type: filesandordirs; Name: "{app}\Logs"
Type: filesandordirs; Name: "{app}\Config"
Type: filesandordirs; Name: "{app}\Cache"
Type: dirifempty; Name: "{app}"
```

- [ ] **Step 4: Add the cross-reference comment to `deploy/patch/source.txt`**

Insert as the new first line (SFF accepts `//` line comments, `sff_read_scanner.h:180`; do not write `from = "..."` inside the comment, `package_client.py` regex-scans the text):

```
// Every top-level entry here is downloaded into the install folder; list it in [UninstallDelete] of deploy/installer/setup.iss too.
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `python tools/tests/test_installer_uninstall_list.py`
Expected: `Ran 4 tests ... OK`

Run: `python tools/tests/test_release_tools.py`
Expected: `OK` (the comment line did not change what `package_client` reads).

- [ ] **Step 6: Commit**

```bash
git add deploy/installer/setup.iss deploy/patch/source.txt tools/tests/test_installer_uninstall_list.py
git commit -m "Add Inno Setup script for the client setup"
```

---

### Task 2: Build wrapper and smoke test

**Files:**
- Create: `deploy/installer/build_setup.ps1`
- Create: `deploy/installer/smoke_test.ps1`

**Interfaces:**
- Consumes: `deploy/installer/setup.iss` (defines `AppVersion`, `LauncherExe`).
- Produces:
  - `build_setup.ps1 -Version <X.Y.Z> -Launcher <path> [-OutDir <dir>]` → writes `<OutDir>\AlestiaOnline-Setup.exe` (default `OutDir` = `dist`), writes that full path to the pipeline, exit 0; throws if ISCC is missing or compilation fails.
  - `smoke_test.ps1 -Setup <path> -Launcher <path> [-AllUsers]` → exit 0 if all scenarios pass, 1 otherwise; prints `PASS`/`FAIL` per scenario.

- [ ] **Step 1: Make Inno Setup available locally**

Check: `Test-Path "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"`. On 2026-10-15 it was not installed on the dev machine. Installing downloads a third-party installer: **ask the user first**, then run `winget install --id JRSoftware.InnoSetup -e --scope user` (installs to `%LOCALAPPDATA%\Programs\Inno Setup 6`). If the user declines, skip to Task 3 and let CI do the first build and smoke test.

- [ ] **Step 2: Write `deploy/installer/smoke_test.ps1` first**

```powershell
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Silent install / upgrade / uninstall checks for AlestiaOnline-Setup.exe. Exit 0 = all passed.
#   pwsh deploy/installer/smoke_test.ps1 -Setup dist/AlestiaOnline-Setup.exe -Launcher bin/Release/Launcher.exe
#   -AllUsers additionally installs for all users (needs an elevated shell; CI runners are).

param(
	[Parameter(Mandatory)][string]$Setup,
	[Parameter(Mandatory)][string]$Launcher,
	[switch]$AllUsers
)

$ErrorActionPreference = "Stop"
$Setup = (Resolve-Path $Setup).Path
$expectedHash = (Get-FileHash -Algorithm SHA256 (Resolve-Path $Launcher).Path).Hash
$failures = 0

function Invoke-Setup([string]$dir, [string]$scope)
{
	$p = Start-Process -FilePath $Setup -Wait -PassThru -ArgumentList @(
		"/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", $scope, '/MERGETASKS="!desktopicon"', "/DIR=""$dir""")
	if ($p.ExitCode -ne 0) { throw "setup exited with $($p.ExitCode)" }
}

function Invoke-Uninstall([string]$dir)
{
	$uninstaller = Join-Path $dir "unins000.exe"
	if (-not (Test-Path $uninstaller)) { throw "no uninstaller in $dir" }
	# unins000.exe copies itself to %TEMP% and returns at once; wait for its files to go.
	Start-Process -FilePath $uninstaller -Wait -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART")
	$deadline = (Get-Date).AddSeconds(60)
	while ((Test-Path $uninstaller) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }
	Start-Sleep -Seconds 1
}

function Add-FakeDownload([string]$dir)
{
	# Stand-ins for what the launcher and client put into the folder.
	New-Item -ItemType Directory -Force (Join-Path $dir "Data\Locales"), (Join-Path $dir "Logs"), (Join-Path $dir "Config"), (Join-Path $dir "Cache") | Out-Null
	foreach ($f in "Data\Models.hpak", "Data\Locales\enUS.hpak", "mmo_client.exe", "mmo_error.exe", "fmod.dll", "launcher.ini", "Launcher.exe.1760000000000", "Logs\Client.log", "Config\Config.cfg", "Cache\Items.db")
	{
		Set-Content -Path (Join-Path $dir $f) -Value "x"
	}
}

function Test-Scenario([string]$name, [scriptblock]$body)
{
	try { & $body; Write-Host "PASS  $name" }
	catch { Write-Host "FAIL  $name -- $_"; $script:failures++ }
}

function New-TestDir([string]$suffix)
{
	# Space and non-ASCII on purpose: player profile paths often have both.
	$dir = Join-Path ([IO.Path]::GetTempPath()) "Alestia Önline Smoke $suffix $([guid]::NewGuid().ToString('N').Substring(0, 6))"
	return $dir
}

Test-Scenario "install, then uninstall removes everything" {
	$dir = New-TestDir "clean"
	Invoke-Setup $dir "/CURRENTUSER"
	$installed = Join-Path $dir "Launcher.exe"
	if (-not (Test-Path $installed)) { throw "Launcher.exe missing" }
	if ((Get-FileHash -Algorithm SHA256 $installed).Hash -ne $expectedHash) { throw "Launcher.exe differs from the packaged binary" }
	Add-FakeDownload $dir
	Invoke-Uninstall $dir
	if (Test-Path $dir) { throw "folder left behind: $((Get-ChildItem -Recurse -Name $dir) -join ', ')" }
}

Test-Scenario "upgrade keeps downloaded data" {
	$dir = New-TestDir "upgrade"
	Invoke-Setup $dir "/CURRENTUSER"
	Add-FakeDownload $dir
	Invoke-Setup $dir "/CURRENTUSER"
	foreach ($f in "Data\Models.hpak", "mmo_client.exe", "Config\Config.cfg")
	{
		if (-not (Test-Path (Join-Path $dir $f))) { throw "$f lost on upgrade" }
	}
	if (Test-Path (Join-Path $dir "unins001.exe")) { throw "second uninstaller: AppId did not match the existing install" }
	Invoke-Uninstall $dir
	if (Test-Path $dir) { throw "folder left behind after upgrade + uninstall" }
}

Test-Scenario "foreign file survives uninstall" {
	$dir = New-TestDir "foreign"
	New-Item -ItemType Directory -Force $dir | Out-Null
	Set-Content -Path (Join-Path $dir "foreign.txt") -Value "not ours"
	Invoke-Setup $dir "/CURRENTUSER"
	Add-FakeDownload $dir
	Invoke-Uninstall $dir
	if (-not (Test-Path (Join-Path $dir "foreign.txt"))) { throw "uninstall deleted a file it did not own" }
	foreach ($f in "Launcher.exe", "Data", "mmo_client.exe", "Config")
	{
		if (Test-Path (Join-Path $dir $f)) { throw "$f left behind" }
	}
	Remove-Item -Recurse -Force $dir
}

if ($AllUsers)
{
	Test-Scenario "all-users install is writable for Users" {
		$dir = Join-Path $env:ProgramFiles "Alestia Online Smoke $([guid]::NewGuid().ToString('N').Substring(0, 6))"
		Invoke-Setup $dir "/ALLUSERS"
		$sid = New-Object System.Security.Principal.SecurityIdentifier("S-1-5-32-545")   # BUILTIN\Users
		$rule = (Get-Acl $dir).Access | Where-Object {
			$_.IdentityReference.Translate([System.Security.Principal.SecurityIdentifier]) -eq $sid -and
			$_.AccessControlType -eq "Allow" -and
			($_.FileSystemRights -band [System.Security.AccessControl.FileSystemRights]::Modify) -eq [System.Security.AccessControl.FileSystemRights]::Modify
		}
		if (-not $rule) { throw "Users have no Modify right on $dir" }
		Invoke-Uninstall $dir
		if (Test-Path $dir) { throw "folder left behind" }
	}
}

if ($failures -gt 0) { Write-Host "$failures scenario(s) failed"; exit 1 }
Write-Host "all scenarios passed"
exit 0
```

- [ ] **Step 3: Run it against a missing setup to see it fail**

Run: `pwsh deploy/installer/smoke_test.ps1 -Setup dist/AlestiaOnline-Setup.exe -Launcher bin/Release/Launcher.exe`
Expected: error `Cannot find path ...dist\AlestiaOnline-Setup.exe` (nothing built yet). If `bin/Release/Launcher.exe` does not exist either, any built launcher works (`bin/Debug/Launcher.exe`), or build it: `cmake --build build --config Release -t launcher` (needs `MMO_BUILD_LAUNCHER=ON` in the build's cache; otherwise use any small `.exe` as a stand-in — the setup does not care what it packages).

- [ ] **Step 4: Write `deploy/installer/build_setup.ps1`**

```powershell
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Compiles deploy/installer/setup.iss into AlestiaOnline-Setup.exe and prints its path.
#   pwsh deploy/installer/build_setup.ps1 -Version 1.0.1 -Launcher bin/Release/Launcher.exe

param(
	[Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
	[Parameter(Mandatory)][string]$Launcher,
	[string]$OutDir = "dist"
)

$ErrorActionPreference = "Stop"

function Find-Iscc
{
	$onPath = Get-Command iscc -ErrorAction SilentlyContinue
	if ($onPath) { return $onPath.Source }
	foreach ($candidate in @(
		"${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
		"$env:ProgramFiles\Inno Setup 6\ISCC.exe",
		"$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"))
	{
		if (Test-Path $candidate) { return $candidate }
	}
	throw "Inno Setup 6 (ISCC.exe) not found. Install it: winget install --id JRSoftware.InnoSetup -e"
}

$iscc = Find-Iscc
$launcherPath = (Resolve-Path $Launcher).Path
New-Item -ItemType Directory -Force $OutDir | Out-Null
$outPath = (Resolve-Path $OutDir).Path
$script = Join-Path $PSScriptRoot "setup.iss"

& $iscc "/DAppVersion=$Version" "/DLauncherExe=$launcherPath" "/O$outPath" "/Q" $script
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }

$setup = Join-Path $outPath "AlestiaOnline-Setup.exe"
if (-not (Test-Path $setup)) { throw "ISCC reported success but $setup is missing" }
$setup
```

- [ ] **Step 5: Build the setup**

Run: `pwsh deploy/installer/build_setup.ps1 -Version 1.0.1 -Launcher bin/Release/Launcher.exe`
Expected: last line is the full path of `dist\AlestiaOnline-Setup.exe`, exit 0. A compile error names the `setup.iss` line; fix it there.

- [ ] **Step 6: Run the smoke test**

Run: `pwsh deploy/installer/smoke_test.ps1 -Setup dist/AlestiaOnline-Setup.exe -Launcher bin/Release/Launcher.exe`
Expected: three `PASS` lines, `all scenarios passed`, exit 0. (Run `-AllUsers` only from an elevated shell; CI covers it.)

A `FAIL ... folder left behind: <list>` names exactly which file the uninstall list misses — add it to `[UninstallDelete]` and to the test's expectations in Task 1, never switch to deleting `{app}` recursively.

- [ ] **Step 7: Make sure `dist/` is not committed**

Run: `git status --short`
Expected: no `dist/` entries. If it shows up, add `/dist/` to `.gitignore`.

- [ ] **Step 8: Commit**

```bash
git add deploy/installer/build_setup.ps1 deploy/installer/smoke_test.ps1
git commit -m "Add setup build wrapper and install/uninstall smoke test"
```

---

### Task 3: Build, test and publish the setup in the launcher release

**Files:**
- Modify: `.github/workflows/launcher-release.yml:68-97`

**Interfaces:**
- Consumes: `build_setup.ps1 -Version -Launcher -OutDir`, `smoke_test.ps1 -Setup -Launcher -AllUsers` (Task 2); `steps.version.outputs.version` (existing "Check the tag" step).
- Produces: release asset `AlestiaOnline-Setup.exe` on every `launcher-v*` release.

- [ ] **Step 1: Insert two steps between "Package" and "Publish release"**

```yaml
      - name: Build setup
        shell: pwsh
        run: |
          $known = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
          if (-not (Get-Command iscc -ErrorAction SilentlyContinue) -and -not (Test-Path $known)) {
            choco install innosetup -y --no-progress
          }
          deploy/installer/build_setup.ps1 -Version "${{ steps.version.outputs.version }}" -Launcher bin/Release/Launcher.exe -OutDir dist

      - name: Smoke-test setup
        shell: pwsh
        run: deploy/installer/smoke_test.ps1 -Setup dist/AlestiaOnline-Setup.exe -Launcher bin/Release/Launcher.exe -AllUsers
```

- [ ] **Step 2: Publish the setup with the release**

In "Publish release", extend the asset list of `gh release create` (line 97):

```bash
            --notes-file notes.md dist/Launcher.exe dist/launcher-release.json dist/launcher-symbols.zip dist/AlestiaOnline-Setup.exe
```

Do not add `--prerelease`: the website link resolves `releases/latest`, which must be the launcher release.

- [ ] **Step 3: Update the header comment**

Lines 3-5 become:

```yaml
# The launcher has its own release cycle (docs/versioning.md): pushing a launcher-vX.Y.Z tag
# builds it and publishes a release the deployer ships with the next nightly patch, plus
# AlestiaOnline-Setup.exe for the website (docs/deployment.md). Nightly builds never change the
# launcher players run, so launchers only update themselves on purpose.
```

- [ ] **Step 4: Validate the workflow file**

Run: `python -c "import yaml,sys; yaml.safe_load(open('.github/workflows/launcher-release.yml', encoding='utf-8')); print('ok')"`
Expected: `ok`. (If PyYAML is missing, `gh workflow view launcher-release.yml` after landing is the fallback; the real test is the next `launcher-v*` tag — see Task 5.)

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/launcher-release.yml
git commit -m "Build and smoke-test the client setup in launcher releases"
```

---

### Task 4: Documentation

**Files:**
- Create: `deploy/installer/README.md`
- Modify: `docs/versioning.md:98-100`
- Modify: `docs/deployment.md` (after line 53; §7 CI notes)

- [ ] **Step 1: Write `deploy/installer/README.md`**

````markdown
# Client setup

`setup.iss` builds `AlestiaOnline-Setup.exe` (Inno Setup 6). The setup installs only
`Launcher.exe` into a folder the player picks; the launcher then downloads the game there.
Per-user install (`%LOCALAPPDATA%\Programs\Alestia Online`, no UAC) is the default; "all users"
installs to Program Files and makes the folder writable for Users so the launcher can patch it.

Design: [docs/superpowers/specs/2026-10-15-client-setup-design.md](../../docs/superpowers/specs/2026-10-15-client-setup-design.md)

## Build locally

Install Inno Setup 6 once (`winget install --id JRSoftware.InnoSetup -e`), then:

```powershell
pwsh deploy/installer/build_setup.ps1 -Version 1.0.1 -Launcher bin/Release/Launcher.exe
pwsh deploy/installer/smoke_test.ps1 -Setup dist/AlestiaOnline-Setup.exe -Launcher bin/Release/Launcher.exe
```

`-AllUsers` on the smoke test also checks an all-users install (elevated shell only).

## Rules

- Never change `AppId` in `setup.iss`: a new id makes upgrades install side by side.
- Uninstall removes only what the launcher and client put into the folder, listed one by one in
  `[UninstallDelete]`. A new top-level entry in `deploy/patch/source.txt` must be added there;
  `tools/tests/test_installer_uninstall_list.py` fails until it is.
- CI builds and smoke-tests the setup for every `launcher-v*` tag (`.github/workflows/launcher-release.yml`).
````

- [ ] **Step 2: Update `docs/versioning.md` (Launcher releases)**

Replace the bullet at lines 98-100 with:

```markdown
- Pushing a `launcher-vX.Y.Z` tag runs `.github/workflows/launcher-release.yml`. It builds
  `Launcher.exe`, builds and smoke-tests `AlestiaOnline-Setup.exe` (see
  [deploy/installer/README.md](../deploy/installer/README.md)), and publishes a release with the
  binary, the setup and `launcher-release.json` (version, commit, sha256, size).
```

- [ ] **Step 3: Update `docs/deployment.md`**

After the paragraph at line 53 (patch built from `source.txt`), add:

```markdown
Every top-level entry of `source.txt` is downloaded into the player's install folder, so it must
also be listed in `[UninstallDelete]` of `deploy/installer/setup.iss`
(`tools/tests/test_installer_uninstall_list.py` enforces this).

The website's client download links to the setup of the newest launcher release:
`https://github.com/Kyoril/mmo/releases/latest/download/AlestiaOnline-Setup.exe`. That URL
resolves to the newest non-prerelease, so every release other than `launcher-v*` must stay a
prerelease (the nightly workflow publishes with `--prerelease`).
```

In §7 "CI notes", add one line:

```markdown
- `launcher-release.yml` installs Inno Setup via Chocolatey if the runner lacks it, then builds and smoke-tests the setup (install, upgrade, foreign-file, all-users, uninstall) before publishing.
```

- [ ] **Step 4: Commit**

```bash
git add deploy/installer/README.md docs/versioning.md docs/deployment.md
git commit -m "Document the client setup"
```

---

### Task 5: Gate, land, and first release (with the user)

**Files:** none

- [ ] **Step 1: Run the fast gate**

Run: `/gate` (or `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -Tier fast`)
Expected: green; `tool_tests` includes `test_installer_uninstall_list`.

- [ ] **Step 2: Manual check on a real Windows desktop (user)**

Hand the user `dist/AlestiaOnline-Setup.exe` and this list:
1. Double-click → "only for me" → default folder → finish with "Launch" checked: launcher opens and downloads the game into `%LOCALAPPDATA%\Programs\Alestia Online`.
2. Start menu and desktop shortcut start the launcher; the game starts from it.
3. "Apps & Features" → Alestia Online → Uninstall: folder is gone, shortcuts gone.
4. Again with "all users" → Program Files: after install, the launcher (started normally, not as admin) completes a full download.
5. Run the setup a second time over an existing install: no re-download of game data.

- [ ] **Step 3: Land**

After the user's OK: `/ship` (merges `feature/client-setup` into develop after a green gate). Do not push unless the user asks.

- [ ] **Step 4: First published setup (user decision)**

The setup appears with the next `launcher-v*` tag. Options to offer the user: tag `launcher-v1.0.1` (also rolls an identical launcher out to players via self-update), or backfill `launcher-v1.0.0` with a setup built around that release's own binary, so the setup ships exactly the launcher players already have:

```powershell
gh release download launcher-v1.0.0 -p Launcher.exe -D dist/v1.0.0
pwsh deploy/installer/build_setup.ps1 -Version 1.0.0 -Launcher dist/v1.0.0/Launcher.exe -OutDir dist/v1.0.0
pwsh deploy/installer/smoke_test.ps1 -Setup dist/v1.0.0/AlestiaOnline-Setup.exe -Launcher dist/v1.0.0/Launcher.exe
gh release upload launcher-v1.0.0 dist/v1.0.0/AlestiaOnline-Setup.exe
```
 Either needs the user's explicit go-ahead (pushing a tag / uploading to a release). Then the user points the website download at the stable URL.
