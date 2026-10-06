# Nightly Automated Deployment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every night where `develop` moved and the full gate is green, CI publishes a nightly release, and a deployer on the root server stages the client patch, runs a 15-minute realm shutdown at 04:45, switches patch and server images together, verifies health and rolls back automatically on failure.

**Architecture:** A GitHub Actions workflow (`nightly-release.yml`) runs the full gate on Linux, builds the Windows client and SHA-tagged Docker images, and publishes a `nightly-YYYYMMDD-<sha8>` prerelease carrying `release.json`, `client-bin.zip` and `symbols.zip`. A stdlib-only Python service (`mmo-deployer`, own Portainer stack) polls those releases, builds the patch locally with `update_compiler` from a git checkout of `mmo-data`, and drives the maintenance through the realm REST API, the Portainer API and an atomic symlink swap of the nginx patch root.

**Tech Stack:** GitHub Actions, PowerShell 7 / Windows PowerShell 5.1, CMake + Ninja Multi-Config (Linux CI), Python 3.12 stdlib (`urllib`, `unittest`, `zipfile`, `zoneinfo`), Docker / Portainer CE API, nginx.

**Spec:** `docs/superpowers/specs/2026-10-06-nightly-deploy-design.md`

**Deliberate deviations from the spec** (each is a refinement, not a scope change):

| Spec says | Plan does | Why |
|---|---|---|
| pytest | stdlib `unittest` | Repo convention (`tools/tests`), the deployer image has no pip, and the gate discovers `unittest` suites. |
| MySQL 8 service container | Runner's preinstalled MySQL (`root`/`root`) | Fewer moving parts; a service container is the documented fallback in Task 4. |
| Stage "the newest release that is not live/staged/bad" | Only the single newest release is considered | Falling back to an older release could deploy something older than live. A bad newest release waits for the next night. |
| `deploy/test/compose.yml` local integration | In-process integration test with stub HTTP servers (Task 11) | Runs inside the gate on every change instead of by hand. |
| — | `NOTIFY_FORMAT` (`discord`/`slack`) | The two webhook payloads differ (`content` vs `text`). |
| — | Launcher news lives in `/srv/mmo-patch/launcher/` (nginx `alias`) | `/launcher/` must survive the `current` symlink flip. |
| `deployer rollback` | Restarts immediately (SIGTERM, graceful save), no countdown | A manual rollback is an emergency action. |

## Global Constraints

- Every new `.py`, `.ps1`, `.psm1` and `.sh` file starts with `# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- Python uses tabs for indentation (matches `tools/`), stdlib only, Python ≥ 3.9 (`zoneinfo`). The deployer image is Ubuntu 24.04 (Python 3.12).
- PowerShell changes must keep working under Windows PowerShell 5.1 (the local `/gate`), and the new code paths must work under `pwsh` on Linux.
- Release tags: `nightly-YYYYMMDD-<sha8>` from `develop`; `test-nightly-YYYYMMDD-<sha8>` from any other branch. The deployer only reads `nightly-`.
- Docker image tags used by the deployer are the full 40-character commit SHA. The deployer never deploys `latest`.
- Defaults: `MAINTENANCE_AT=04:45`, `TZ=Europe/Berlin`, `COUNTDOWN_S=900`, `POLL_INTERVAL_S=900`, `KEEP_RELEASES=3`, `KEEP_BACKUPS=7`, post-deploy verification 300 s, container-exit wait `COUNTDOWN_S + 1800` s, maintenance window 3600 s.
- Work happens on `feature/nightly-deploy`. **Never push to origin without the user's explicit yes** (CLAUDE.md). Task 4 has a checkpoint for this.
- No wire-format change anywhere in this plan → no `ProtocolVersion` bump.

## Review Focus

1. **Deployer (re)started outside the maintenance window** (e.g. at 14:00 after a host reboot) with a staged release: it must not shut the realms down until the next 04:45. Pinned in Task 10 (`test_restart_in_the_afternoon_does_not_trigger_maintenance`).
2. **A realm that crash-loops after deploy** can answer `/uptime` once between restarts: verification needs two consecutive healthy rounds, otherwise roll back. Pinned in Task 9 (`test_crash_looping_realm_is_rolled_back`).
3. **The release pins a data commit `mmo-data` does not have** (or git is unreachable): staging fails, staged/live stay unchanged, and the user is notified once, not every 15 minutes. Pinned in Task 7 (`test_unknown_data_commit_fails_cleanly`) and Task 10 (`test_stage_failure_notifies_once`).
4. **The newest release is marked bad while an older nightly exists:** nothing is staged, so the deployer never goes backwards. Pinned in Task 7 (`test_bad_newest_release_does_not_fall_back_to_older`).
5. **`launcher.json` growing past the launcher's 256 KiB / 32-entry limits** after many nights: the oldest patch notes are dropped. Pinned in Task 8 (`test_manifest_stays_within_launcher_limits`).

---

## File Structure

```
.github/workflows/
  docker-build.yml          NEW  reusable: build+push all images for a ref with given tags
  docker.yml                MOD  develop builds only after "Linux Servers" passes; adds full-SHA tag
  nightly-release.yml       NEW  check → gate (Linux) → client (Windows) + images → publish
deploy/
  patch/source.txt          NEW  update_compiler input (salvaged from feature/automated-patch-pipeline)
  deployer/
    compose.yml             NEW  Portainer stack for the deployer
    deployer.sh             NEW  `deployer` CLI shim inside the image
    smoke_update_compiler.sh NEW real update_compiler smoke test (runs in the image)
    mmo_deployer/
      __init__.py  __main__.py
      config.py    env → Config
      state.py     state.json (atomic)
      clock.py     wall clock (FakeClock in tests)
      web.py       urllib wrapper, HttpError, basic_auth
      clients.py   GitHubClient, RestApi, PortainerClient, Notifier
      patchdir.py  releases/<sha>, `current` symlink flip, prune
      stage.py     select_release, Stager
      backup.py    mysqldump, prune_backups
      news.py      launcher.json patch notes
      maintenance.py  Maintenance (shutdown → flip → redeploy → verify → rollback)
      app.py       Deployer loop, request queue
      cli.py       argparse entry, build_deployer wiring
    tests/
      __init__.py fakes.py stub_server.py fake_update_compiler.py
      test_config.py test_state.py test_clients.py test_stage.py test_backup.py
      test_news.py test_maintenance.py test_app.py test_integration.py
tools/
  release/package_client.py        NEW  client-bin.zip from bin/Release per source.txt
  release/make_release_manifest.py NEW  release.json
  tests/test_release_tools.py      NEW
  gate/verify.ps1                  MOD  Linux-capable, deployer_tests step
  e2e/e2e_common.psm1, e2e_up.ps1, e2e_run.ps1  MOD  Linux-capable
Dockerfile.deployer, Dockerfile.deployer.dockerignore  NEW
compose.yml                        MOD  ${MMO_TAG}, data sync, depends_on, stable network name
docs/deployment.md                 NEW  runbook
CLAUDE.md                          MOD  additive-migration rule, deployment pointer
```

---

## Phase 1 — CI release production

### Task 1: Linux-capable gate and E2E harness

**Files:**
- Modify: `tools/e2e/e2e_common.psm1` (lines 10, 14, 63-77, 263-276, export list)
- Modify: `tools/e2e/e2e_up.ps1` (lines 27, 41-49, 60-61, 69-72, 78-79, 259, 278, 294, 301)
- Modify: `tools/e2e/e2e_run.ps1` (lines 26-28, 40, 150-156)
- Modify: `tools/gate/verify.ps1` (lines 35, 140, 176)

**Interfaces:**
- Produces: `Test-OnWindows` → `[bool]`; `Get-ExeName([string]$Name)` → `"$Name.exe"` on Windows, `$Name` elsewhere (both exported from `e2e_common.psm1`). Task 4's Linux gate job depends on all of this.

- [ ] **Step 1: Add platform helpers to `tools/e2e/e2e_common.psm1`**

Replace line 10 and line 14 path literals with forward slashes (they work on Windows too):

```powershell
	$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
```
```powershell
		RuntimeDir        = Join-Path $repoRoot "e2e/runtime"
```

Insert after `Get-E2eSettings` (before `Get-Sha1Hex`):

```powershell
# Windows PowerShell 5.1 has no $IsWindows (and strict mode rejects reading it), but it
# only ever runs on Windows.
function Test-OnWindows
{
	if ($PSVersionTable.PSEdition -eq "Desktop")
	{
		return $true
	}
	return [bool]$IsWindows
}

# Executable file name for a CMake target: login_server.exe on Windows, login_server elsewhere.
function Get-ExeName([string]$Name)
{
	if (Test-OnWindows)
	{
		return "$Name.exe"
	}
	return $Name
}
```

Replace `Get-MysqlExe` (lines 62-77) with:

```powershell
# Locates the mysql client or aborts with an actionable message.
function Get-MysqlExe
{
	$name = Get-ExeName "mysql"
	$cmd = Get-Command $name -ErrorAction SilentlyContinue
	if ($cmd)
	{
		return $cmd.Source
	}
	if (Test-OnWindows)
	{
		# Try common install locations before giving up.
		$candidates = Get-ChildItem -Path "C:\Program Files\MySQL\*\bin\mysql.exe", "C:\Program Files\MariaDB*\bin\mysql.exe" -ErrorAction SilentlyContinue
		if ($candidates)
		{
			return $candidates[0].FullName
		}
	}
	throw "$name not found on PATH. Install the MySQL/MariaDB client or add it to PATH."
}
```

In `Start-E2eServer`, replace the `Start-Process` call (lines 273-274) with a splat, because `-WindowStyle` throws on Linux:

```powershell
	$startArgs = @{
		FilePath = $ExePath
		ArgumentList = $Arguments
		WorkingDirectory = $WorkingDirectory
		PassThru = $true
		RedirectStandardOutput = $StdoutPath
		RedirectStandardError = $stderrPath
	}
	if (Test-OnWindows)
	{
		$startArgs.WindowStyle = "Hidden"
	}
	$process = Start-Process @startArgs
```

Extend the `Export-ModuleMember` list with `Test-OnWindows, Get-ExeName`.

- [ ] **Step 2: Make `tools/e2e/e2e_up.ps1` platform-neutral**

```powershell
$binDir = Join-Path (Join-Path $s.RepoRoot "bin") $BuildConfig
```

Replace the executable check loop (lines 43-49):

```powershell
foreach ($target in @("login_server", "realm_server", "world_server"))
{
	if (-not (Test-Path (Join-Path $binDir (Get-ExeName $target))))
	{
		throw "$(Get-ExeName $target) not found in $binDir. Build it first: cmake --build build -t $target --config $BuildConfig"
	}
}
```

Forward slashes in the literal sub-paths on lines 60-61, 69-72, 78-79, e.g.:

```powershell
Invoke-MysqlScriptFile -Path (Join-Path $s.RepoRoot "data/login/login_db_schema_full.sql") -Database $s.LoginDb -StripUse
Invoke-MysqlScriptFile -Path (Join-Path $s.RepoRoot "data/realm/realm_db_schema_full.sql") -Database $s.RealmDb -StripUse
```
```powershell
$dataDir = (Join-Path $s.RepoRoot "data/editor/data") -replace '\\', '/'
$navDir = (Join-Path $s.RepoRoot "data/editor/nav") -replace '\\', '/'
$worldDataDir = (Join-Path $s.RepoRoot "data/client") -replace '\\', '/'
$scriptsDir = (Join-Path $s.RepoRoot "data/scripts") -replace '\\', '/'
```
```powershell
$loginUpdates = (Join-Path $s.RepoRoot "data/login/updates") -replace '\\', '/'
$realmUpdates = (Join-Path $s.RepoRoot "data/realm/updates") -replace '\\', '/'
```

Lines 259, 278, 294: `(Join-Path $binDir (Get-ExeName "login_server"))`, `(Get-ExeName "realm_server")`, `(Get-ExeName "world_server")`.

Line 301: `-PathGlob (Join-Path $runtime "world/logs/*.log")`.

- [ ] **Step 3: Make `tools/e2e/e2e_run.ps1` platform-neutral**

```powershell
$binDir = Join-Path (Join-Path $s.RepoRoot "bin") $BuildConfig
$clientExe = Join-Path $binDir (Get-ExeName "e2e_client")
$scenarioDir = Join-Path $s.RepoRoot "e2e/scenarios"
```

Line 40: `throw "$(Split-Path -Leaf $clientExe) not found in $binDir. Build it first: cmake --build build -t e2e_client --config $BuildConfig"`

Replace the scenario `Start-Process` (lines 150-156):

```powershell
			$clientArgs = @{
				FilePath = $clientExe
				ArgumentList = (@(
					"--config", $clientConfig,
					"--script", $file.FullName,
					"--transcript", $transcript,
					"--character", $characterName,
					"--timeout", "$timeoutSeconds"
				) + $classArgs)
				WorkingDirectory = $s.RepoRoot
				PassThru = $true
				Wait = $true
				RedirectStandardOutput = $stdout
			}
			if (Test-OnWindows)
			{
				$clientArgs.WindowStyle = "Hidden"
			}
			$process = Start-Process @clientArgs
```

- [ ] **Step 4: Make `tools/gate/verify.ps1` platform-neutral**

Line 35: `$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path`

After line 35 add:

```powershell
# Windows PowerShell 5.1 has no $IsWindows; it only runs on Windows.
$onWindows = ($PSVersionTable.PSEdition -eq "Desktop") -or [bool]$IsWindows
```

Line 140 (the skills sync check depends on Windows junctions and backslash paths):

```powershell
	if ($onWindows)
	{
		& (Join-Path $repoRoot "tools/sync_skills.ps1") -Check
	}
```

Line 176: run E2E with the same PowerShell host the gate runs in (`powershell` on Windows, `pwsh` on Linux):

```powershell
			$shellExe = (Get-Process -Id $PID).Path
			$null = Invoke-GateStep -Name "e2e" -Exe $shellExe -Arguments @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $repoRoot "tools/e2e/e2e_run.ps1"))
```

- [ ] **Step 5: Check the helpers under Linux pwsh**

Run:
```bash
docker run --rm -v "C:/Kyoril/mmo/tools:/tools" mcr.microsoft.com/powershell:latest pwsh -NoProfile -Command "Import-Module /tools/e2e/e2e_common.psm1; Get-ExeName login_server; Test-OnWindows; (Get-E2eSettings).RuntimeDir"
```
Expected: `login_server`, `False`, a path ending in `/e2e/runtime`.

- [ ] **Step 6: Windows regression — full gate**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -Tier full`
Expected: `Gate result: GREEN`, with the `e2e` step passing. That shows the Windows path did not regress. Linux E2E is proven in Task 4.

- [ ] **Step 7: Commit**

```bash
git add tools/e2e tools/gate/verify.ps1
git commit -m "build(gate): make the gate and E2E harness runnable under pwsh on Linux"
```

---

### Task 2: Patch source and release tooling

**Files:**
- Create: `deploy/patch/source.txt`
- Create: `tools/release/package_client.py`
- Create: `tools/release/make_release_manifest.py`
- Test: `tools/tests/test_release_tools.py`

**Interfaces:**
- Produces (CLI, used by `nightly-release.yml` in Task 4):
  - `python tools/release/package_client.py --bin-dir bin/Release --source deploy/patch/source.txt --out client-bin.zip` → a zip with `source.txt` + every top-level binary named in it.
  - `python tools/release/make_release_manifest.py --repo . --commit <sha> --previous <sha|""> --asset NAME=PATH ... --gate-report <json> --out release.json`
- Produces (release.json schema 1, consumed by the deployer in Task 7): keys `schema`, `commit`, `image_tag`, `data_client_commit`, `data_editor_commit`, `created_at`, `assets.{name}.sha256|size`, `gate.passed|steps[]`, `changes[]`.

- [ ] **Step 1: Create `deploy/patch/source.txt`**

This is `data/patch/source.txt` from `origin/feature/automated-patch-pipeline`. `../../data/client` resolves to the repo's `data/client` both in the repo and in the deployer workspace:

```
version = 0
root = (type = "fs", from = ".", to = "", entries =
{
	(type = "if", condition = "WINDOWS", value =
		(type = "fs", from = "Launcher.exe")
	)
	(type = "fs", from = "../../data/client", to = "Data", entries =
	{
		(type = "hpak2", from = "Config", to = "Misc.hpak", sub = "Config")
		(type = "hpak2", from = "Fonts", to = "Fonts.hpak", sub = "Fonts")
		(type = "hpak2", from = "Interface", to = "Interface.hpak", sub = "Interface")
		(type = "hpak2", from = "Models", to = "Models.hpak", sub = "Models")
		(type = "hpak2", from = "Sound", to = "Sound.hpak", sub = "Sound")
		(type = "hpak2", from = "Textures", to = "Textures.hpak", sub = "Textures")
		(type = "hpak2", from = "ClientDB", to = "ClientDB.hpak", sub = "ClientDB")
		(type = "hpak2", from = "Worlds", to = "Worlds.hpak", sub = "Worlds")
		(type = "fs", from = "Locales", to = "Locales", entries =
		{
			(type = "hpak2", from = "Locale_deDE", to = "Locale_deDE.hpak")
			(type = "hpak2", from = "Locale_enUS", to = "Locale_enUS.hpak")
			(type = "hpak2", from = "Locale_frFR", to = "Locale_frFR.hpak")
			(type = "hpak2", from = "Locale_ruRU", to = "Locale_ruRU.hpak")
		})
	})
	(type = "if", condition = "WINDOWS", value =
		(type = "fs", from = "fmod.dll")
	)
	(type = "if", condition = "WINDOWS", value =
		(type = "fs", from = "mmo_client.exe")
	)
	(type = "if", condition = "WINDOWS", value =
		(type = "fs", from = "mmo_error.exe")
	)
})
```

Then check that every top-level `data/client` folder the client loads is covered:
```bash
ls data/client
```
`Editor`, `Particles` and `Cache` are not packed today. Ask the user before adding any of them, because adding them changes what players download.

- [ ] **Step 2: Write the failing tests `tools/tests/test_release_tools.py`**

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for tools/release/package_client.py and tools/release/make_release_manifest.py."""

import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _load(name):
	path = os.path.join(REPO_ROOT, "tools", "release", name + ".py")
	spec = importlib.util.spec_from_file_location(name, path)
	module = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(module)
	return module


package_client = _load("package_client")
make_release_manifest = _load("make_release_manifest")


def _git(repo, *args):
	return subprocess.run(
		["git", "-C", repo, "-c", "user.name=Test", "-c", "user.email=test@example.com"] + list(args),
		check=True, capture_output=True, text=True).stdout.strip()


class PackageClient(unittest.TestCase):
	def test_real_source_txt_names_the_shipped_binaries(self):
		with open(os.path.join(REPO_ROOT, "deploy", "patch", "source.txt"), encoding="utf-8") as handle:
			names = package_client.binaries_from_source(handle.read())
		self.assertEqual(names, ["Launcher.exe", "fmod.dll", "mmo_client.exe", "mmo_error.exe"])

	def test_package_zips_source_and_binaries(self):
		with tempfile.TemporaryDirectory() as tmp:
			bin_dir = os.path.join(tmp, "bin")
			os.makedirs(bin_dir)
			for name in ("a.exe", "b.dll", "unrelated.exe"):
				with open(os.path.join(bin_dir, name), "wb") as handle:
					handle.write(name.encode())
			source = os.path.join(tmp, "source.txt")
			with open(source, "w", encoding="utf-8") as handle:
				handle.write('root = (entries = { (type = "fs", from = "a.exe") (type = "fs", from = "b.dll") (type = "fs", from = "../data", to = "Data") })')
			out = os.path.join(tmp, "client-bin.zip")

			names = package_client.package(bin_dir, source, out)

			self.assertEqual(names, ["a.exe", "b.dll"])
			with zipfile.ZipFile(out) as archive:
				self.assertEqual(sorted(archive.namelist()), ["a.exe", "b.dll", "source.txt"])

	def test_missing_binary_is_an_error(self):
		with tempfile.TemporaryDirectory() as tmp:
			source = os.path.join(tmp, "source.txt")
			with open(source, "w", encoding="utf-8") as handle:
				handle.write('(type = "fs", from = "missing.exe")')
			with self.assertRaises(FileNotFoundError) as ctx:
				package_client.package(tmp, source, os.path.join(tmp, "out.zip"))
			self.assertIn("missing.exe", str(ctx.exception))


class ReleaseManifest(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.repo = self.tmp.name
		_git(self.repo, "init", "-q")
		_git(self.repo, "commit", "-q", "--allow-empty", "-m", "first")
		self.first = _git(self.repo, "rev-parse", "HEAD")
		_git(self.repo, "update-index", "--add", "--cacheinfo", "160000," + "1" * 40 + ",data/client")
		_git(self.repo, "update-index", "--add", "--cacheinfo", "160000," + "2" * 40 + ",data/editor")
		_git(self.repo, "commit", "-q", "-m", "fix(loot): roll on the right table")
		_git(self.repo, "commit", "-q", "--allow-empty", "-m", "feat(chat): emotes")
		self.head = _git(self.repo, "rev-parse", "HEAD")

	def tearDown(self):
		self.tmp.cleanup()

	def test_changes_since_previous_exclude_older_commits(self):
		changes = make_release_manifest.collect_changes(self.repo, self.first, self.head)
		self.assertEqual(changes, ["feat(chat): emotes", "fix(loot): roll on the right table"])

	def test_changes_without_previous_are_capped(self):
		changes = make_release_manifest.collect_changes(self.repo, "", self.head, limit=2)
		self.assertEqual(len(changes), 2)

	def test_submodule_commits(self):
		self.assertEqual(make_release_manifest.submodule_commit(self.repo, self.head, "data/client"), "1" * 40)
		self.assertEqual(make_release_manifest.submodule_commit(self.repo, self.head, "data/editor"), "2" * 40)

	def test_build_manifest(self):
		asset = os.path.join(self.repo, "client-bin.zip")
		with open(asset, "wb") as handle:
			handle.write(b"zipdata")
		report = {"passed": True, "steps": [{"name": "e2e", "passed": True, "exit_code": 0, "log": "x", "duration_s": 12.5}]}

		manifest = make_release_manifest.build_manifest(
			self.repo, self.head, self.first, {"client-bin.zip": asset}, report, "2026-10-07T00:52:11Z")

		self.assertEqual(manifest["schema"], 1)
		self.assertEqual(manifest["commit"], self.head)
		self.assertEqual(manifest["image_tag"], self.head)
		self.assertEqual(manifest["data_client_commit"], "1" * 40)
		self.assertEqual(manifest["assets"]["client-bin.zip"]["sha256"], hashlib.sha256(b"zipdata").hexdigest())
		self.assertEqual(manifest["gate"], {"passed": True, "steps": [{"name": "e2e", "passed": True, "duration_s": 12.5}]})
		self.assertEqual(len(manifest["changes"]), 2)
		json.dumps(manifest)


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `python tools/tests/test_release_tools.py`
Expected: FAIL with `FileNotFoundError` for `tools/release/package_client.py`.

- [ ] **Step 4: Implement `tools/release/package_client.py`**

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Packs the client binaries named by deploy/patch/source.txt into client-bin.zip.

The zip carries source.txt itself, so the deployer compiles the patch with exactly the
layout of the commit it deploys.
"""

import argparse
import re
import sys
import zipfile
from pathlib import Path

# A top-level file entry: from = "name.exe" / "name.dll" (no path separators).
_BINARY = re.compile(r'from\s*=\s*"([^"/\\]+\.(?:exe|dll))"')


def binaries_from_source(text):
	return sorted(set(_BINARY.findall(text)))


def package(bin_dir, source_txt, out_zip):
	text = Path(source_txt).read_text(encoding="utf-8")
	names = binaries_from_source(text)
	if not names:
		raise ValueError("{} names no binaries".format(source_txt))
	missing = [name for name in names if not (Path(bin_dir) / name).is_file()]
	if missing:
		raise FileNotFoundError("missing in {}: {}".format(bin_dir, ", ".join(missing)))
	with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as archive:
		archive.write(source_txt, "source.txt")
		for name in names:
			archive.write(Path(bin_dir) / name, name)
	return names


def main(argv=None):
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--bin-dir", required=True)
	parser.add_argument("--source", required=True)
	parser.add_argument("--out", required=True)
	args = parser.parse_args(argv)
	names = package(args.bin_dir, args.source, args.out)
	print("packed {} into {}".format(", ".join(names), args.out))
	return 0


if __name__ == "__main__":
	sys.exit(main())
```

- [ ] **Step 5: Implement `tools/release/make_release_manifest.py`**

```python
#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Writes release.json, the contract between the nightly release workflow and the deployer."""

import argparse
import datetime
import hashlib
import json
import os
import subprocess
import sys

DEFAULT_CHANGE_LIMIT = 200
FIRST_RELEASE_CHANGES = 20


def _git(repo, *args):
	return subprocess.run(["git", "-C", repo] + list(args), check=True, capture_output=True, text=True).stdout


def submodule_commit(repo, commit, path):
	# "160000 commit <sha>\t<path>"
	return _git(repo, "ls-tree", commit, path).split()[2]


def collect_changes(repo, previous, commit, limit=DEFAULT_CHANGE_LIMIT):
	"""Non-merge commit subjects since `previous` (newest first); the last few if there is none."""
	if previous:
		args = ["log", "--no-merges", "--format=%s", "{}..{}".format(previous, commit)]
	else:
		args = ["log", "--no-merges", "--format=%s", "-n", str(FIRST_RELEASE_CHANGES), commit]
	lines = [line.strip() for line in _git(repo, *args).splitlines() if line.strip()]
	return lines[:limit]


def sha256_file(path):
	digest = hashlib.sha256()
	with open(path, "rb") as handle:
		for chunk in iter(lambda: handle.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


def build_manifest(repo, commit, previous, assets, gate_report, created_at):
	gate = None
	if gate_report is not None:
		gate = {
			"passed": bool(gate_report.get("passed")),
			"steps": [{"name": s["name"], "passed": s["passed"], "duration_s": s["duration_s"]} for s in gate_report.get("steps", [])],
		}
	return {
		"schema": 1,
		"commit": commit,
		"image_tag": commit,
		"data_client_commit": submodule_commit(repo, commit, "data/client"),
		"data_editor_commit": submodule_commit(repo, commit, "data/editor"),
		"created_at": created_at,
		"assets": {name: {"sha256": sha256_file(path), "size": os.path.getsize(path)} for name, path in assets.items()},
		"gate": gate,
		"changes": collect_changes(repo, previous, commit),
	}


def main(argv=None):
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", default=".")
	parser.add_argument("--commit", required=True)
	parser.add_argument("--previous", default="")
	parser.add_argument("--asset", action="append", default=[], help="NAME=PATH")
	parser.add_argument("--gate-report")
	parser.add_argument("--out", required=True)
	args = parser.parse_args(argv)

	assets = dict(item.split("=", 1) for item in args.asset)
	report = None
	if args.gate_report:
		with open(args.gate_report, encoding="utf-8-sig") as handle:
			report = json.load(handle)
		if not report.get("passed"):
			print("gate report is not green; refusing to write a release manifest", file=sys.stderr)
			return 1
	created_at = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
	manifest = build_manifest(args.repo, args.commit, args.previous, assets, report, created_at)
	with open(args.out, "w", encoding="utf-8") as handle:
		json.dump(manifest, handle, indent=2)
	print("wrote {} ({} changes)".format(args.out, len(manifest["changes"])))
	return 0


if __name__ == "__main__":
	sys.exit(main())
```

`utf-8-sig` is there because `verify.ps1` writes `last_report.json` via `Set-Content -Encoding UTF8`, which adds a BOM on Windows PowerShell.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `python tools/tests/test_release_tools.py`
Expected: `OK` (7 tests).

- [ ] **Step 7: Commit**

```bash
git add deploy/patch/source.txt tools/release tools/tests/test_release_tools.py
git commit -m "feat(release): patch source and release manifest tooling"
```

---

### Task 3: Reusable Docker build, gated develop images, SHA tags

**Files:**
- Create: `.github/workflows/docker-build.yml`
- Modify: `.github/workflows/docker.yml` (full rewrite)

**Interfaces:**
- Produces: reusable workflow `./.github/workflows/docker-build.yml` with inputs `ref` (commit to build) and `tags` (newline-separated tag names). It pushes `<DOCKER_USERNAME>/<image>:<tag>` for every image in its matrix. Callers pass `secrets: inherit`. Task 4 calls it with `tags: <sha>`. Task 12 adds `mmo_deployer` to the matrix.

- [ ] **Step 1: Create `.github/workflows/docker-build.yml`**

```yaml
name: Docker Build

on:
  workflow_call:
    inputs:
      ref:
        description: Commit to build
        type: string
        required: true
      tags:
        description: Newline-separated image tags to push
        type: string
        required: true

jobs:
  build:
    name: Build and push ${{ matrix.image }}
    runs-on: ubuntu-latest
    environment: DockerHub
    strategy:
      fail-fast: true
      matrix:
        include:
          - image: mmo_login_server
            dockerfile: ./Dockerfile.login_server
          - image: mmo_realm_server
            dockerfile: ./Dockerfile.realm_server
          - image: mmo_world_server
            dockerfile: ./Dockerfile.world_server
          - image: mmo_data
            dockerfile: ./Dockerfile.data
    steps:
      - name: Check out the repo
        uses: actions/checkout@v4
        with:
          ref: ${{ inputs.ref }}
          submodules: recursive

      - name: Set up Docker Buildx
        uses: docker/setup-buildx-action@v3

      - name: Login to DockerHub
        uses: docker/login-action@v3
        with:
          username: ${{ secrets.DOCKER_USERNAME }}
          password: ${{ secrets.DOCKER_PASSWORD }}

      - name: Compute image tags
        id: tags
        shell: bash
        run: |
          {
            echo "list<<EOF"
            while IFS= read -r tag; do
              if [ -n "$tag" ]; then
                echo "${{ secrets.DOCKER_USERNAME }}/${{ matrix.image }}:$tag"
              fi
            done <<< "${{ inputs.tags }}"
            echo "EOF"
          } >> "$GITHUB_OUTPUT"

      - name: Build and push
        uses: docker/build-push-action@v6
        with:
          context: .
          file: ${{ matrix.dockerfile }}
          push: true
          tags: ${{ steps.tags.outputs.list }}
          cache-from: type=gha,scope=${{ matrix.image }}
          cache-to: type=gha,mode=max,scope=${{ matrix.image }}
```

- [ ] **Step 2: Rewrite `.github/workflows/docker.yml`**

Develop images are built only after "Linux Servers" passes, and only for the commit that passed. The old version also built on every develop push and checked out the default-branch HEAD instead of `workflow_run.head_sha`.

```yaml
name: Publish Docker Images

on:
  push:
    branches:
      - master
  workflow_run:
    workflows: ["Linux Servers"]
    types:
      - completed
    branches:
      - develop

jobs:
  resolve:
    if: github.event_name == 'push' || github.event.workflow_run.conclusion == 'success'
    runs-on: ubuntu-latest
    outputs:
      ref: ${{ steps.resolve.outputs.ref }}
      tags: ${{ steps.resolve.outputs.tags }}
    steps:
      - id: resolve
        shell: bash
        run: |
          if [ "${{ github.event_name }}" = "workflow_run" ]; then
            sha="${{ github.event.workflow_run.head_sha }}"
            branch="${{ github.event.workflow_run.head_branch }}"
          else
            sha="${{ github.sha }}"
            branch="${{ github.ref_name }}"
          fi
          echo "ref=$sha" >> "$GITHUB_OUTPUT"
          {
            echo "tags<<EOF"
            echo "$branch"
            echo "$branch-${sha:0:7}"
            echo "$sha"
            if [ "$branch" = "develop" ]; then
              echo "latest"
            fi
            echo "EOF"
          } >> "$GITHUB_OUTPUT"

  build:
    needs: resolve
    uses: ./.github/workflows/docker-build.yml
    with:
      ref: ${{ needs.resolve.outputs.ref }}
      tags: ${{ needs.resolve.outputs.tags }}
    secrets: inherit
```

- [ ] **Step 3: Lint both workflows**

Run: `docker run --rm -v "C:/Kyoril/mmo:/repo" -w /repo rhysd/actionlint:latest -color .github/workflows/docker.yml .github/workflows/docker-build.yml`
Expected: no output, exit 0.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/docker-build.yml .github/workflows/docker.yml
git commit -m "ci(docker): build develop images only after tests pass, tag with full commit SHA"
```

---

### Task 4: Nightly release workflow

**Files:**
- Create: `.github/workflows/nightly-release.yml`

**Interfaces:**
- Consumes: `tools/gate/verify.ps1` (Task 1), `tools/release/*.py` and `deploy/patch/source.txt` (Task 2), `docker-build.yml` (Task 3).
- Produces: GitHub prerelease `<prefix>YYYYMMDD-<sha8>` with `target_commitish = <full sha>` and assets `release.json`, `client-bin.zip`, `symbols.zip`. Docker images `:<full sha>`. The deployer (Task 7) reads `tag_name`, `target_commitish`, `created_at`, `draft`, `assets[].name|id`.

- [ ] **Step 1: Create `.github/workflows/nightly-release.yml`**

```yaml
name: Nightly Release

on:
  schedule:
    - cron: "30 0 * * *"
  workflow_dispatch:
    inputs:
      force:
        description: Release even if develop has not moved since the last nightly
        type: boolean
        default: false
  # TEMPORARY: lets the feature branch prove the pipeline (publishes test-nightly-*).
  # Removed in Task 15.
  push:
    branches:
      - feature/nightly-deploy

concurrency:
  group: nightly-release
  cancel-in-progress: false

permissions:
  contents: write

jobs:
  check:
    runs-on: ubuntu-latest
    outputs:
      commit: ${{ steps.pick.outputs.commit }}
      previous: ${{ steps.pick.outputs.previous }}
      prefix: ${{ steps.pick.outputs.prefix }}
      proceed: ${{ steps.pick.outputs.proceed }}
    steps:
      - uses: actions/checkout@v4
        with:
          ref: ${{ github.event_name == 'push' && github.ref || 'develop' }}
          fetch-depth: 0

      - id: pick
        shell: bash
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          commit=$(git rev-parse HEAD)
          if [ "${{ github.event_name }}" = "push" ]; then prefix="test-nightly-"; else prefix="nightly-"; fi
          latest=$(gh release list --limit 100 --json tagName,createdAt \
            --jq "[.[] | select(.tagName | startswith(\"$prefix\"))] | sort_by(.createdAt) | last | .tagName // empty")
          previous=""
          if [ -n "$latest" ]; then previous=$(git rev-list -n 1 "$latest"); fi
          proceed=true
          if [ "$previous" = "$commit" ] && [ "${{ inputs.force }}" != "true" ]; then proceed=false; fi
          echo "Nightly candidate $commit (previous ${previous:-none}, proceed $proceed)"
          {
            echo "commit=$commit"
            echo "previous=$previous"
            echo "prefix=$prefix"
            echo "proceed=$proceed"
          } >> "$GITHUB_OUTPUT"

  gate:
    needs: check
    if: needs.check.outputs.proceed == 'true'
    runs-on: ubuntu-24.04
    timeout-minutes: 180
    env:
      CC: gcc-13
      CXX: g++-13
      MMO_GATE_PYTHON: python3
      MMO_E2E_MYSQL_USER: root
      MMO_E2E_MYSQL_PASSWORD: root
    steps:
      - name: Free disk space (data/client is ~8 GB)
        run: |
          sudo rm -rf /usr/share/dotnet /usr/local/lib/android /opt/ghc /opt/hostedtoolcache/CodeQL
          df -h /

      - uses: actions/checkout@v4
        with:
          ref: ${{ needs.check.outputs.commit }}
          submodules: recursive

      - name: Install toolchain
        run: |
          sudo apt-get update
          sudo apt-get install -y gcc-13 g++-13 ninja-build libmysqlclient-dev uuid-dev

      - name: Start MySQL (preinstalled on the runner, root/root)
        run: |
          sudo systemctl start mysql.service
          mysql -uroot -proot -h127.0.0.1 -e "SELECT VERSION();"

      - name: Configure
        run: cmake -S . -B build -G "Ninja Multi-Config" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DMMO_WITH_DEV_COMMANDS=ON

      - name: Full gate
        shell: pwsh
        run: ./tools/gate/verify.ps1 -Tier full

      - name: Upload gate report
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: gate-report
          if-no-files-found: ignore
          path: |
            tools/gate/last_report.json
            tools/gate/logs/
            e2e/runtime/logs/

  client:
    needs: [check, gate]
    runs-on: windows-latest
    timeout-minutes: 120
    steps:
      - uses: actions/checkout@v4
        with:
          ref: ${{ needs.check.outputs.commit }}

      - name: Init code submodules (not the data repos)
        run: git submodule update --init deps/asio deps/imgui deps/glfw deps/recastnavigation deps/json

      - name: vcpkg build
        uses: johnwason/vcpkg-action@v6
        with:
          pkgs: openssl
          triplet: x64-windows-static-md
          token: ${{ github.token }}
          github-binarycache: true

      - name: Configure CMake
        shell: pwsh
        run: |
          cmake -S . -B build `
            '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' `
            '-DMMO_BUILD_CLIENT=ON' `
            '-DMMO_BUILD_LAUNCHER=ON' `
            '-DMMO_BUILD_TOOLS=ON' `
            '-DMMO_BUILD_TESTS=OFF' `
            '-DMMO_WITH_DEV_COMMANDS=ON' `
            '-DMMO_OPENSSL_STATIC=ON' `
            '-DOPENSSL_USE_STATIC_LIBS=TRUE' `
            '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md' `
            "-DCMAKE_TOOLCHAIN_FILE=${{ github.workspace }}/vcpkg/scripts/buildsystems/vcpkg.cmake"

      - name: Build client, launcher, crash reporter
        run: cmake --build build --config Release --parallel 4 -t mmo_client launcher mmo_error

      - name: Package
        shell: pwsh
        run: |
          python tools/release/package_client.py --bin-dir bin/Release --source deploy/patch/source.txt --out client-bin.zip
          Compress-Archive -Path bin/Release/*.pdb -DestinationPath symbols.zip

      - uses: actions/upload-artifact@v4
        with:
          name: client-bin
          path: |
            client-bin.zip
            symbols.zip

  images:
    needs: [check, gate]
    uses: ./.github/workflows/docker-build.yml
    with:
      ref: ${{ needs.check.outputs.commit }}
      tags: ${{ needs.check.outputs.commit }}
    secrets: inherit

  publish:
    needs: [check, gate, client, images]
    runs-on: ubuntu-latest
    env:
      COMMIT: ${{ needs.check.outputs.commit }}
      PREVIOUS: ${{ needs.check.outputs.previous }}
      PREFIX: ${{ needs.check.outputs.prefix }}
    steps:
      - uses: actions/checkout@v4
        with:
          ref: ${{ needs.check.outputs.commit }}
          fetch-depth: 0

      - uses: actions/download-artifact@v4
        with:
          name: client-bin
          path: dist

      - uses: actions/download-artifact@v4
        with:
          name: gate-report
          path: gate

      - name: Build release.json
        run: |
          python3 tools/release/make_release_manifest.py --repo . --commit "$COMMIT" --previous "$PREVIOUS" \
            --asset client-bin.zip=dist/client-bin.zip --asset symbols.zip=dist/symbols.zip \
            --gate-report gate/tools/gate/last_report.json --out dist/release.json

      - name: Publish prerelease
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          tag="${PREFIX}$(date -u +%Y%m%d)-${COMMIT:0:8}"
          python3 -c "import json; m = json.load(open('dist/release.json')); print('\n'.join('- ' + c for c in m['changes']) or '- No changes')" > notes.md
          gh release create "$tag" --target "$COMMIT" --prerelease --title "$tag" --notes-file notes.md \
            dist/client-bin.zip dist/symbols.zip dist/release.json
```

- [ ] **Step 2: Lint**

Run: `docker run --rm -v "C:/Kyoril/mmo:/repo" -w /repo rhysd/actionlint:latest -color .github/workflows/nightly-release.yml`
Expected: no output, exit 0.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/nightly-release.yml
git commit -m "ci: nightly release workflow (Linux gate, Windows client, SHA-tagged images)"
```

- [ ] **Step 4: CHECKPOINT — ask the user before pushing**

Ask: "Task 4 needs `feature/nightly-deploy` on origin so the workflow can run (it publishes a `test-nightly-*` prerelease and `:<sha>` images, and is ignored by the deployer). May I push the branch?" **Do not push without an explicit yes.** On yes:

```bash
git push -u origin feature/nightly-deploy
```

- [ ] **Step 5: Watch the run and confirm every job is green**

Run: `gh run watch $(gh run list --workflow nightly-release.yml --branch feature/nightly-deploy --limit 1 --json databaseId --jq ".[0].databaseId")`
Expected: `check`, `gate`, `client`, `images`, `publish` all succeed, and `gh release view test-nightly-<date>-<sha8>` lists the three assets.

If `gate` fails, download the `gate-report` artifact (`gh run download <id> -n gate-report`) and read `tools/gate/logs/<step>.log` and `e2e/runtime/logs/`. The likely Linux-only causes are:
- a server binary or `e2e_client` failing to build with gcc-13 Debug: fix the code
- the e2e stack failing to connect to MySQL: check `mysql -uroot -proot -h127.0.0.1` in the log
- a scenario timing out: compare it with the Windows run

Fix the cause, commit, and push again (the earlier yes covers re-pushing this branch).

If E2E cannot work on Linux, use the fallback: move the `gate` job to `runs-on: windows-latest` and replace the MySQL and toolchain steps with:
```yaml
      - uses: shogo82148/actions-setup-mysql@v1
        with:
          mysql-version: "8.0"
          root-password: root
```
Also drop `-G "Ninja Multi-Config"` and the CC/CXX env vars. Write down which path was taken in the Task 14 runbook.

- [ ] **Step 6: Confirm the published manifest**

Run: `gh release download test-nightly-<date>-<sha8> -p release.json -O - | python -m json.tool`
Expected: `schema` 1, `commit` equal to the branch HEAD, 40-character `data_client_commit`, `gate.passed` true, non-empty `assets.client-bin.zip.sha256`.

---

## Phase 2 — Deployer: foundation and staging

### Task 5: Deployer foundation (config, state, clock, HTTP) and gate step

**Files:**
- Create: `deploy/deployer/mmo_deployer/__init__.py`, `config.py`, `state.py`, `clock.py`, `web.py`
- Create: `deploy/deployer/tests/__init__.py`, `deploy/deployer/tests/fakes.py`
- Test: `deploy/deployer/tests/test_config.py`, `deploy/deployer/tests/test_state.py`
- Modify: `tools/gate/verify.ps1` (new `deployer_tests` step after `tool_tests`)

**Interfaces:**
- Produces:
  - `config.load_config(env: Mapping[str, str]) -> Config` (raises `ConfigError`). `Config` is a frozen dataclass with fields `github_repo, github_token, release_prefix, data_repo_url, update_compiler, compiler_threads, portainer_url, portainer_token, portainer_stack_id, portainer_endpoint_id, wait_services: tuple[str], realms: tuple[RealmEndpoint], login_url, login_password, mysql_host, mysql_user, mysql_password, backup_databases: tuple[str], maintenance_at: datetime.time, timezone, countdown_s, poll_interval_s, keep_releases, keep_backups, notify_webhook, notify_format, dry_run: bool, patch_root: Path, state_dir: Path`. `RealmEndpoint(name, url, password)`.
  - `state.State` dataclass (`live, previous_live, staged, staged_tag, staged_changes, bad, paused, last_maintenance_date, history`), `load_state(path) -> State`, `save_state(path, state)`, `record(state, now, event, **details)`, `HISTORY_LIMIT = 100`.
  - `clock.Clock(tz_name)` with `now() -> aware datetime`, `sleep(seconds)`.
  - `web.Http(timeout)` with `request`, `get_json`, `send_json`, `post_form`, `download`. `web.HttpError(status, body, url)` and `web.basic_auth(user, password) -> dict`.
  - `tests/fakes.py`: `BASE_ENV`, `make_config(tmp, **overrides)`, `FakeClock(start)`. Later tasks append to this file.

- [ ] **Step 1: Write `deploy/deployer/tests/__init__.py` and `deploy/deployer/tests/fakes.py`**

`tests/__init__.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
```

`tests/fakes.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Shared test doubles for the deployer tests."""

import dataclasses
import datetime
import json
from pathlib import Path

from mmo_deployer.config import load_config

BASE_ENV = {
	"GITHUB_REPO": "Kyoril/mmo",
	"DATA_REPO_URL": "https://github.com/Kyoril/mmo-data",
	"PORTAINER_URL": "https://portainer.local",
	"PORTAINER_TOKEN": "token",
	"PORTAINER_STACK_ID": "7",
	"PORTAINER_ENDPOINT_ID": "2",
	"REALMS": json.dumps([{"name": "realm01", "url": "http://mmo-dev-realm-01:8092/", "password": "pw"}]),
	"LOGIN_URL": "http://mmo-dev-login:8090",
	"LOGIN_PASSWORD": "pw",
	"MYSQL_HOST": "host.docker.internal",
	"MYSQL_USER": "deployer",
	"MYSQL_PASSWORD": "secret",
	"BACKUP_DATABASES": "mmo_login,mmo_realm_01",
}


def make_config(tmp, **overrides):
	cfg = load_config(BASE_ENV)
	return dataclasses.replace(cfg, patch_root=Path(tmp) / "patch", state_dir=Path(tmp) / "state", **overrides)


class FakeClock:
	def __init__(self, start):
		self.current = start
		self.slept = 0

	def now(self):
		return self.current

	def sleep(self, seconds):
		self.current += datetime.timedelta(seconds=seconds)
		self.slept += seconds
```

- [ ] **Step 2: Write the failing tests**

`tests/test_config.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

import datetime
import unittest

from mmo_deployer.config import ConfigError, load_config

from .fakes import BASE_ENV


class LoadConfig(unittest.TestCase):
	def test_defaults(self):
		cfg = load_config(BASE_ENV)
		self.assertEqual(cfg.maintenance_at, datetime.time(4, 45))
		self.assertEqual(cfg.countdown_s, 900)
		self.assertEqual(cfg.poll_interval_s, 900)
		self.assertEqual(cfg.keep_releases, 3)
		self.assertEqual(cfg.keep_backups, 7)
		self.assertEqual(cfg.release_prefix, "nightly-")
		self.assertEqual(cfg.timezone, "Europe/Berlin")
		self.assertEqual(cfg.wait_services, ("realm_server_01", "world_node_01"))
		self.assertEqual(cfg.backup_databases, ("mmo_login", "mmo_realm_01"))
		self.assertEqual(cfg.portainer_stack_id, 7)
		self.assertEqual(cfg.notify_format, "discord")
		self.assertFalse(cfg.dry_run)

	def test_realm_urls_lose_trailing_slash(self):
		cfg = load_config(BASE_ENV)
		self.assertEqual(cfg.realms[0].url, "http://mmo-dev-realm-01:8092")
		self.assertEqual(cfg.realms[0].name, "realm01")

	def test_missing_variables_are_all_named(self):
		env = dict(BASE_ENV)
		del env["PORTAINER_TOKEN"]
		del env["REALMS"]
		with self.assertRaises(ConfigError) as ctx:
			load_config(env)
		self.assertIn("PORTAINER_TOKEN", str(ctx.exception))
		self.assertIn("REALMS", str(ctx.exception))

	def test_bad_realms_json(self):
		with self.assertRaises(ConfigError):
			load_config(dict(BASE_ENV, REALMS="not json"))

	def test_empty_realms(self):
		with self.assertRaises(ConfigError):
			load_config(dict(BASE_ENV, REALMS="[]"))

	def test_overrides(self):
		cfg = load_config(dict(BASE_ENV, MAINTENANCE_AT="05:30", DRY_RUN="1", NOTIFY_FORMAT="slack"))
		self.assertEqual(cfg.maintenance_at, datetime.time(5, 30))
		self.assertTrue(cfg.dry_run)
		self.assertEqual(cfg.notify_format, "slack")

	def test_bad_values(self):
		for key, value in (("MAINTENANCE_AT", "25:99"), ("COUNTDOWN_S", "soon"), ("NOTIFY_FORMAT", "pigeon")):
			with self.subTest(key=key):
				with self.assertRaises(ConfigError):
					load_config(dict(BASE_ENV, **{key: value}))


if __name__ == "__main__":
	unittest.main()
```

`tests/test_state.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

import datetime
import json
import os
import tempfile
import unittest
from pathlib import Path

from mmo_deployer.state import HISTORY_LIMIT, State, load_state, record, save_state


class StateFile(unittest.TestCase):
	def setUp(self):
		self.dir = tempfile.TemporaryDirectory()
		self.path = Path(self.dir.name) / "state.json"

	def tearDown(self):
		self.dir.cleanup()

	def test_missing_file_is_empty_state(self):
		state = load_state(self.path)
		self.assertIsNone(state.live)
		self.assertEqual(state.bad, [])
		self.assertFalse(state.paused)

	def test_round_trip(self):
		state = State(live="a", staged="b", staged_changes=["x"], bad=["c"], paused=True)
		save_state(self.path, state)
		self.assertEqual(load_state(self.path), state)

	def test_unknown_keys_are_ignored(self):
		self.path.write_text(json.dumps({"live": "a", "future_field": 1}), encoding="utf-8")
		self.assertEqual(load_state(self.path).live, "a")

	def test_save_leaves_no_temp_file(self):
		save_state(self.path, State(live="a"))
		self.assertEqual(sorted(os.listdir(self.dir.name)), ["state.json"])

	def test_history_is_capped(self):
		state = State()
		now = datetime.datetime(2026, 10, 7, tzinfo=datetime.timezone.utc)
		for index in range(HISTORY_LIMIT + 5):
			record(state, now, "event", index=index)
		self.assertEqual(len(state.history), HISTORY_LIMIT)
		self.assertEqual(state.history[0]["index"], 5)
		self.assertEqual(state.history[-1]["event"], "event")


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `python -m unittest discover -s deploy/deployer/tests -t deploy/deployer -p "test_*.py"`
Expected: FAIL with `ModuleNotFoundError: No module named 'mmo_deployer'`.

- [ ] **Step 4: Implement the modules**

`mmo_deployer/__init__.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""mmo-deployer: stages nightly releases and runs the nightly maintenance."""
```

`mmo_deployer/config.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Deployer configuration, read once from environment variables."""

import datetime
import json
from dataclasses import dataclass
from pathlib import Path

REQUIRED = (
	"GITHUB_REPO", "DATA_REPO_URL",
	"PORTAINER_URL", "PORTAINER_TOKEN", "PORTAINER_STACK_ID", "PORTAINER_ENDPOINT_ID",
	"REALMS", "LOGIN_URL", "LOGIN_PASSWORD",
	"MYSQL_HOST", "MYSQL_USER", "MYSQL_PASSWORD", "BACKUP_DATABASES",
)
NOTIFY_FORMATS = ("discord", "slack")


class ConfigError(Exception):
	pass


@dataclass(frozen=True)
class RealmEndpoint:
	name: str
	url: str
	password: str


@dataclass(frozen=True)
class Config:
	github_repo: str
	github_token: str
	release_prefix: str
	data_repo_url: str
	update_compiler: str
	compiler_threads: int
	portainer_url: str
	portainer_token: str
	portainer_stack_id: int
	portainer_endpoint_id: int
	wait_services: tuple
	realms: tuple
	login_url: str
	login_password: str
	mysql_host: str
	mysql_user: str
	mysql_password: str
	backup_databases: tuple
	maintenance_at: datetime.time
	timezone: str
	countdown_s: int
	poll_interval_s: int
	keep_releases: int
	keep_backups: int
	notify_webhook: str
	notify_format: str
	dry_run: bool
	patch_root: Path
	state_dir: Path


def _csv(text):
	return tuple(item.strip() for item in text.split(",") if item.strip())


def _parse_time(text):
	hours, minutes = text.split(":")
	return datetime.time(int(hours), int(minutes))


def _parse_realms(text):
	try:
		realms = tuple(RealmEndpoint(r["name"], r["url"].rstrip("/"), r["password"]) for r in json.loads(text))
	except (ValueError, KeyError, TypeError, AttributeError) as error:
		raise ConfigError("REALMS must be a JSON list of {{name, url, password}}: {}".format(error))
	if not realms:
		raise ConfigError("REALMS must name at least one realm")
	return realms


def load_config(env):
	missing = [name for name in REQUIRED if not env.get(name)]
	if missing:
		raise ConfigError("missing environment variables: " + ", ".join(missing))
	realms = _parse_realms(env["REALMS"])
	notify_format = env.get("NOTIFY_FORMAT", "discord")
	if notify_format not in NOTIFY_FORMATS:
		raise ConfigError("NOTIFY_FORMAT must be one of " + ", ".join(NOTIFY_FORMATS))
	try:
		return Config(
			github_repo=env["GITHUB_REPO"],
			github_token=env.get("GITHUB_TOKEN", ""),
			release_prefix=env.get("RELEASE_PREFIX", "nightly-"),
			data_repo_url=env["DATA_REPO_URL"],
			update_compiler=env.get("UPDATE_COMPILER", "/app/update_compiler"),
			compiler_threads=int(env.get("COMPILER_THREADS", "2")),
			portainer_url=env["PORTAINER_URL"].rstrip("/"),
			portainer_token=env["PORTAINER_TOKEN"],
			portainer_stack_id=int(env["PORTAINER_STACK_ID"]),
			portainer_endpoint_id=int(env["PORTAINER_ENDPOINT_ID"]),
			wait_services=_csv(env.get("WAIT_SERVICES", "realm_server_01,world_node_01")),
			realms=realms,
			login_url=env["LOGIN_URL"].rstrip("/"),
			login_password=env["LOGIN_PASSWORD"],
			mysql_host=env["MYSQL_HOST"],
			mysql_user=env["MYSQL_USER"],
			mysql_password=env["MYSQL_PASSWORD"],
			backup_databases=_csv(env["BACKUP_DATABASES"]),
			maintenance_at=_parse_time(env.get("MAINTENANCE_AT", "04:45")),
			timezone=env.get("TZ", "Europe/Berlin"),
			countdown_s=int(env.get("COUNTDOWN_S", "900")),
			poll_interval_s=int(env.get("POLL_INTERVAL_S", "900")),
			keep_releases=int(env.get("KEEP_RELEASES", "3")),
			keep_backups=int(env.get("KEEP_BACKUPS", "7")),
			notify_webhook=env.get("NOTIFY_WEBHOOK", ""),
			notify_format=notify_format,
			dry_run=env.get("DRY_RUN", "0") == "1",
			patch_root=Path(env.get("PATCH_ROOT", "/srv/mmo-patch")),
			state_dir=Path(env.get("STATE_DIR", "/state")),
		)
	except ValueError as error:
		raise ConfigError(str(error))
```

`mmo_deployer/state.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Persistent deployer state (state.json), always written atomically."""

import json
import os
from dataclasses import asdict, dataclass, field, fields
from typing import Optional

HISTORY_LIMIT = 100


@dataclass
class State:
	live: Optional[str] = None
	previous_live: Optional[str] = None
	staged: Optional[str] = None
	staged_tag: Optional[str] = None
	staged_changes: list = field(default_factory=list)
	bad: list = field(default_factory=list)
	paused: bool = False
	last_maintenance_date: Optional[str] = None
	history: list = field(default_factory=list)


def load_state(path):
	if not os.path.exists(path):
		return State()
	with open(path, encoding="utf-8") as handle:
		data = json.load(handle)
	known = {f.name for f in fields(State)}
	return State(**{key: value for key, value in data.items() if key in known})


def save_state(path, state):
	tmp = "{}.tmp".format(path)
	with open(tmp, "w", encoding="utf-8") as handle:
		json.dump(asdict(state), handle, indent=2)
	os.replace(tmp, path)


def record(state, now, event, **details):
	entry = {"time": now.isoformat(), "event": event}
	entry.update(details)
	state.history.append(entry)
	del state.history[:-HISTORY_LIMIT]
```

`mmo_deployer/clock.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Wall clock in the configured time zone; tests substitute FakeClock."""

import datetime
import time
import zoneinfo


class Clock:
	def __init__(self, tz_name):
		self.tz = zoneinfo.ZoneInfo(tz_name)

	def now(self):
		return datetime.datetime.now(self.tz)

	def sleep(self, seconds):
		time.sleep(seconds)
```

`mmo_deployer/web.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Minimal HTTP helper on urllib: stdlib only, so the image needs no pip install."""

import base64
import json
import shutil
import urllib.error
import urllib.parse
import urllib.request


class HttpError(Exception):
	"""HTTP status >= 400. Connection failures surface as OSError instead."""

	def __init__(self, status, body, url):
		super().__init__("HTTP {} from {}: {}".format(status, url, body[:200]))
		self.status = status
		self.body = body


def basic_auth(user, password):
	token = base64.b64encode("{}:{}".format(user, password).encode("utf-8")).decode("ascii")
	return {"Authorization": "Basic " + token}


class Http:
	def __init__(self, timeout=60):
		self.timeout = timeout

	def _open(self, method, url, headers, body, secret_headers):
		request = urllib.request.Request(url, data=body, method=method)
		for key, value in (headers or {}).items():
			request.add_header(key, value)
		# Never forwarded on redirects: GitHub asset downloads redirect to a storage host
		# that rejects a second Authorization header.
		for key, value in (secret_headers or {}).items():
			request.add_unredirected_header(key, value)
		try:
			return urllib.request.urlopen(request, timeout=self.timeout)
		except urllib.error.HTTPError as error:
			raise HttpError(error.code, error.read().decode("utf-8", "replace"), url) from None

	def request(self, method, url, headers=None, body=None, secret_headers=None):
		"""Returns (status, body bytes); raises HttpError for status >= 400."""
		with self._open(method, url, headers, body, secret_headers) as response:
			return response.status, response.read()

	def get_json(self, url, headers=None, secret_headers=None):
		_, body = self.request("GET", url, headers, None, secret_headers)
		return json.loads(body.decode("utf-8"))

	def send_json(self, method, url, payload, headers=None, secret_headers=None):
		merged = dict(headers or {})
		merged["Content-Type"] = "application/json"
		_, body = self.request(method, url, merged, json.dumps(payload).encode("utf-8"), secret_headers)
		return json.loads(body.decode("utf-8")) if body else None

	def post_form(self, url, fields, headers=None, secret_headers=None):
		merged = dict(headers or {})
		merged["Content-Type"] = "application/x-www-form-urlencoded"
		body = urllib.parse.urlencode(fields).encode("ascii")
		_, data = self.request("POST", url, merged, body, secret_headers)
		return json.loads(data.decode("utf-8")) if data else None

	def download(self, url, dest, headers=None, secret_headers=None):
		with self._open("GET", url, headers, None, secret_headers) as response, open(dest, "wb") as out:
			shutil.copyfileobj(response, out, 1024 * 1024)
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `python -m unittest discover -s deploy/deployer/tests -t deploy/deployer -p "test_*.py"`
Expected: `OK` (12 tests).

- [ ] **Step 6: Add the gate step in `tools/gate/verify.ps1`**

Insert directly after the `tool_tests` block:

```powershell
	# The deployer ships to the live server; its tests gate every merge like the C++ suites.
	if ($ok)
	{
		$ok = Invoke-GateStep -Name "deployer_tests" -Exe $python -Arguments @("-m", "unittest", "discover", "-s", "deploy/deployer/tests", "-t", "deploy/deployer", "-p", "test_*.py")
	}
```

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -Tier fast`
Expected: `Gate result: GREEN`, with a `deployer_tests` step in `tools/gate/last_report.json`.

- [ ] **Step 7: Commit**

```bash
git add deploy/deployer tools/gate/verify.ps1
git commit -m "feat(deployer): config, state, clock and HTTP foundation"
```

---

### Task 6: API clients (GitHub, realm/login REST, Portainer, notifications)

**Files:**
- Create: `deploy/deployer/mmo_deployer/clients.py`
- Create: `deploy/deployer/tests/stub_server.py`
- Test: `deploy/deployer/tests/test_clients.py`

**Interfaces:**
- Consumes: `web.Http`, `web.HttpError`, `web.basic_auth` (Task 5).
- Produces:
  - `GitHubClient(repo, token, http, api="https://api.github.com")` with:
    - `list_releases(prefix) -> list[dict]` (newest first, no drafts)
    - `download_asset(release, name, dest)` (raises `KeyError` if the asset is absent)
    - `fetch_manifest(release, scratch_dir) -> dict`
  - `RestApi(name, url, password, http)` with:
    - `uptime() -> int`
    - `shutdown_status() -> dict`
    - `schedule_shutdown(delay) -> bool` (False when the realm answers 409, i.e. it is already shutting down)
    - `cancel_shutdown()` (409 is ignored)
  - `PortainerClient(url, token, stack_id, endpoint_id, http)` with:
    - `ping()`
    - `current_tag() -> str` (default `"latest"`)
    - `redeploy(tag)`
    - `service_states() -> dict[str, str]` (compose service name → container state)
  - `Notifier(url, fmt, http)` with `send(text)` (never raises).
  - `tests/stub_server.py`: `StubServer()` context manager with `.url`, `.route(method, path, response)`, `.find(method, path) -> list[StubRequest]`. A response is `(status, payload)`, `(status, headers, payload)`, or a callable taking a `StubRequest` and returning one of those. `StubRequest` has `method, path, query, headers` (lower-case keys), `body`, `.form()` and `.json()`.

- [ ] **Step 1: Write `tests/stub_server.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""A tiny threaded HTTP server for client and integration tests."""

import json
import threading
import urllib.parse
from collections import namedtuple
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class StubRequest(namedtuple("StubRequest", "method path query headers body")):
	def form(self):
		return {key: values[0] for key, values in urllib.parse.parse_qs(self.body.decode("ascii")).items()}

	def json(self):
		return json.loads(self.body.decode("utf-8"))


def _normalize(result):
	if len(result) == 2:
		status, payload = result
		return status, {}, payload
	return result


class StubServer:
	def __init__(self):
		self.routes = {}
		self.requests = []
		stub = self

		class Handler(BaseHTTPRequestHandler):
			def log_message(self, *args):
				pass

			def _handle(self):
				length = int(self.headers.get("Content-Length") or 0)
				body = self.rfile.read(length) if length else b""
				parsed = urllib.parse.urlsplit(self.path)
				request = StubRequest(
					self.command, parsed.path, urllib.parse.parse_qs(parsed.query),
					{key.lower(): value for key, value in self.headers.items()}, body)
				stub.requests.append(request)
				response = stub.routes.get((self.command, parsed.path))
				if response is None:
					status, headers, payload = 404, {}, {"status": "NOT_FOUND"}
				else:
					status, headers, payload = _normalize(response(request) if callable(response) else response)
				if isinstance(payload, bytes):
					data = payload
				elif payload is None:
					data = b""
				else:
					data = json.dumps(payload).encode("utf-8")
				self.send_response(status)
				for key, value in headers.items():
					self.send_header(key, value)
				self.send_header("Content-Length", str(len(data)))
				self.end_headers()
				self.wfile.write(data)

			do_GET = _handle
			do_POST = _handle
			do_PUT = _handle

		self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
		self.url = "http://127.0.0.1:{}".format(self.server.server_address[1])
		self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

	def route(self, method, path, response):
		self.routes[(method, path)] = response

	def find(self, method, path):
		return [r for r in self.requests if r.method == method and r.path == path]

	def __enter__(self):
		self.thread.start()
		return self

	def __exit__(self, *exc):
		self.server.shutdown()
		self.server.server_close()
```

- [ ] **Step 2: Write the failing tests `tests/test_clients.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

import base64
import json
import os
import tempfile
import unittest

from mmo_deployer.clients import GitHubClient, Notifier, PortainerClient, RestApi
from mmo_deployer.web import Http, HttpError

from .stub_server import StubServer


def _release(tag, created, draft=False, assets=()):
	return {"tag_name": tag, "target_commitish": tag[-8:], "created_at": created, "draft": draft, "assets": list(assets)}


class GitHub(unittest.TestCase):
	def test_list_filters_prefix_and_drafts_newest_first(self):
		with StubServer() as stub:
			stub.route("GET", "/repos/Kyoril/mmo/releases", (200, [
				_release("nightly-20261006-aaaaaaaa", "2026-10-06T00:50:00Z"),
				_release("test-nightly-20261007-bbbbbbbb", "2026-10-07T00:40:00Z"),
				_release("nightly-20261007-cccccccc", "2026-10-07T00:50:00Z"),
				_release("nightly-20261008-dddddddd", "2026-10-08T00:50:00Z", draft=True),
			]))
			client = GitHubClient("Kyoril/mmo", "tok", Http(), stub.url)
			tags = [r["tag_name"] for r in client.list_releases("nightly-")]
			self.assertEqual(tags, ["nightly-20261007-cccccccc", "nightly-20261006-aaaaaaaa"])
			self.assertEqual(stub.requests[0].headers["authorization"], "Bearer tok")

	def test_download_asset_does_not_leak_token_on_redirect(self):
		with StubServer() as stub:
			stub.route("GET", "/repos/Kyoril/mmo/releases/assets/5", (302, {"Location": stub.url + "/storage/blob"}, None))
			stub.route("GET", "/storage/blob", (200, b"payload"))
			client = GitHubClient("Kyoril/mmo", "tok", Http(), stub.url)
			release = _release("nightly-x", "2026-10-07T00:50:00Z", assets=[{"name": "client-bin.zip", "id": 5}])
			with tempfile.TemporaryDirectory() as tmp:
				dest = os.path.join(tmp, "out")
				client.download_asset(release, "client-bin.zip", dest)
				with open(dest, "rb") as handle:
					self.assertEqual(handle.read(), b"payload")
			api_request = stub.find("GET", "/repos/Kyoril/mmo/releases/assets/5")[0]
			self.assertEqual(api_request.headers["accept"], "application/octet-stream")
			self.assertEqual(api_request.headers["authorization"], "Bearer tok")
			self.assertNotIn("authorization", stub.find("GET", "/storage/blob")[0].headers)

	def test_missing_asset(self):
		client = GitHubClient("Kyoril/mmo", "", Http(), "http://127.0.0.1:9")
		with self.assertRaises(KeyError):
			client.download_asset(_release("nightly-x", "t"), "release.json", "unused")


class Rest(unittest.TestCase):
	def test_uptime_uses_basic_auth(self):
		with StubServer() as stub:
			stub.route("GET", "/uptime", (200, {"uptime": 42}))
			api = RestApi("realm01", stub.url, "pw", Http())
			self.assertEqual(api.uptime(), 42)
			expected = "Basic " + base64.b64encode(b"deployer:pw").decode("ascii")
			self.assertEqual(stub.requests[0].headers["authorization"], expected)

	def test_schedule_shutdown_posts_delay(self):
		with StubServer() as stub:
			stub.route("POST", "/shutdown", (200, {"status": "SUCCESS", "delay": 900}))
			self.assertTrue(RestApi("r", stub.url, "pw", Http()).schedule_shutdown(900))
			self.assertEqual(stub.requests[0].form(), {"delay": "900"})

	def test_schedule_shutdown_conflict_means_already_shutting_down(self):
		with StubServer() as stub:
			stub.route("POST", "/shutdown", (409, {"status": "SHUTTING_DOWN"}))
			self.assertFalse(RestApi("r", stub.url, "pw", Http()).schedule_shutdown(900))

	def test_schedule_shutdown_other_errors_raise(self):
		with StubServer() as stub:
			stub.route("POST", "/shutdown", (503, {"status": "UNAVAILABLE"}))
			with self.assertRaises(HttpError):
				RestApi("r", stub.url, "pw", Http()).schedule_shutdown(900)

	def test_cancel_ignores_not_pending(self):
		with StubServer() as stub:
			stub.route("POST", "/shutdown/cancel", (409, {"status": "NOT_PENDING"}))
			RestApi("r", stub.url, "pw", Http()).cancel_shutdown()


class Portainer(unittest.TestCase):
	def _stub(self, stub, env):
		stub.route("GET", "/api/stacks/7", (200, {"Id": 7, "Name": "mmo", "Env": env}))
		stub.route("GET", "/api/stacks/7/file", (200, {"StackFileContent": "services: {}\n"}))
		stub.route("PUT", "/api/stacks/7", (200, {"Id": 7}))

	def test_redeploy_sets_tag_keeps_other_env_and_pulls(self):
		with StubServer() as stub:
			self._stub(stub, [{"name": "MMO_TAG", "value": "old"}, {"name": "OTHER", "value": "1"}])
			PortainerClient(stub.url, "key", 7, 2, Http()).redeploy("newsha")
			put = stub.find("PUT", "/api/stacks/7")[0]
			self.assertEqual(put.query, {"endpointId": ["2"]})
			self.assertEqual(put.headers["x-api-key"], "key")
			body = put.json()
			self.assertEqual(body["stackFileContent"], "services: {}\n")
			self.assertTrue(body["pullImage"])
			self.assertFalse(body["prune"])
			self.assertEqual(sorted((e["name"], e["value"]) for e in body["env"]), [("MMO_TAG", "newsha"), ("OTHER", "1")])

	def test_current_tag_defaults_to_latest(self):
		with StubServer() as stub:
			self._stub(stub, None)
			self.assertEqual(PortainerClient(stub.url, "key", 7, 2, Http()).current_tag(), "latest")

	def test_service_states(self):
		with StubServer() as stub:
			self._stub(stub, [])
			stub.route("GET", "/api/endpoints/2/docker/containers/json", (200, [
				{"Names": ["/mmo-realm_server_01-1"], "State": "exited", "Labels": {"com.docker.compose.service": "realm_server_01"}},
				{"Names": ["/mmo-world_node_01-1"], "State": "running", "Labels": {"com.docker.compose.service": "world_node_01"}},
			]))
			states = PortainerClient(stub.url, "key", 7, 2, Http()).service_states()
			self.assertEqual(states, {"realm_server_01": "exited", "world_node_01": "running"})
			query = stub.find("GET", "/api/endpoints/2/docker/containers/json")[0].query
			self.assertEqual(json.loads(query["filters"][0]), {"label": ["com.docker.compose.project=mmo"]})


class Notify(unittest.TestCase):
	def test_discord_payload(self):
		with StubServer() as stub:
			stub.route("POST", "/hook", (204, None))
			Notifier(stub.url + "/hook", "discord", Http()).send("hello")
			self.assertEqual(stub.requests[0].json(), {"content": "hello"})

	def test_slack_payload(self):
		with StubServer() as stub:
			stub.route("POST", "/hook", (200, None))
			Notifier(stub.url + "/hook", "slack", Http()).send("hello")
			self.assertEqual(stub.requests[0].json(), {"text": "hello"})

	def test_failures_never_raise(self):
		with StubServer() as stub:
			stub.route("POST", "/hook", (500, {"error": "down"}))
			Notifier(stub.url + "/hook", "discord", Http()).send("hello")
		Notifier("http://127.0.0.1:9/hook", "discord", Http(timeout=1)).send("hello")

	def test_no_url_sends_nothing(self):
		Notifier("", "discord", Http()).send("hello")


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `python -m unittest discover -s deploy/deployer/tests -t deploy/deployer -p "test_clients.py"`
Expected: FAIL with `ModuleNotFoundError: No module named 'mmo_deployer.clients'`.

- [ ] **Step 4: Implement `mmo_deployer/clients.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Clients for every remote system the deployer talks to."""

import json
import logging
import os
import urllib.parse

from .web import HttpError, basic_auth

log = logging.getLogger("deployer")

# Discord rejects messages over 2000 characters; Slack accepts more but this keeps them readable.
MESSAGE_LIMIT = 1900


class GitHubClient:
	"""Reads nightly releases. A read-only token is enough; a public repo needs none."""

	def __init__(self, repo, token, http, api="https://api.github.com"):
		self.repo = repo
		self.token = token
		self.http = http
		self.api = api.rstrip("/")

	def _auth(self):
		return {"Authorization": "Bearer " + self.token} if self.token else {}

	def list_releases(self, prefix):
		"""Published releases whose tag starts with `prefix`, newest first."""
		url = "{}/repos/{}/releases?per_page=50".format(self.api, self.repo)
		releases = self.http.get_json(url, {"Accept": "application/vnd.github+json"}, self._auth())
		matching = [r for r in releases if r["tag_name"].startswith(prefix) and not r.get("draft")]
		return sorted(matching, key=lambda r: r["created_at"], reverse=True)

	def download_asset(self, release, name, dest):
		for asset in release.get("assets", []):
			if asset["name"] == name:
				url = "{}/repos/{}/releases/assets/{}".format(self.api, self.repo, asset["id"])
				self.http.download(url, dest, {"Accept": "application/octet-stream"}, self._auth())
				return
		raise KeyError("release {} has no asset {}".format(release["tag_name"], name))

	def fetch_manifest(self, release, scratch_dir):
		path = os.path.join(scratch_dir, "release.json")
		self.download_asset(release, "release.json", path)
		with open(path, encoding="utf-8") as handle:
			return json.load(handle)


class RestApi:
	"""Realm or login server REST API. Both check only the Basic auth password."""

	def __init__(self, name, url, password, http):
		self.name = name
		self.url = url.rstrip("/")
		self.password = password
		self.http = http

	def _auth(self):
		return basic_auth("deployer", self.password)

	def uptime(self):
		return int(self.http.get_json(self.url + "/uptime", None, self._auth())["uptime"])

	def shutdown_status(self):
		return self.http.get_json(self.url + "/shutdown", None, self._auth())

	def schedule_shutdown(self, delay):
		"""True if scheduled; False if the realm is already shutting down (409)."""
		try:
			self.http.post_form(self.url + "/shutdown", {"delay": str(delay)}, None, self._auth())
			return True
		except HttpError as error:
			if error.status == 409:
				return False
			raise

	def cancel_shutdown(self):
		try:
			self.http.post_form(self.url + "/shutdown/cancel", {}, None, self._auth())
		except HttpError as error:
			if error.status != 409:
				raise


class PortainerClient:
	"""Updates one file-based (web editor) compose stack through the Portainer CE API."""

	def __init__(self, url, token, stack_id, endpoint_id, http):
		self.url = url.rstrip("/")
		self.token = token
		self.stack_id = stack_id
		self.endpoint_id = endpoint_id
		self.http = http

	def _auth(self):
		return {"X-API-Key": self.token}

	def _stack(self):
		return self.http.get_json("{}/api/stacks/{}".format(self.url, self.stack_id), None, self._auth())

	def ping(self):
		self.http.get_json(self.url + "/api/status", None, self._auth())

	def current_tag(self):
		for item in self._stack().get("Env") or []:
			if item.get("name") == "MMO_TAG":
				return item.get("value")
		return "latest"

	def redeploy(self, tag):
		"""Sets MMO_TAG and redeploys with image pull. Returns once compose is up."""
		stack = self._stack()
		content = self.http.get_json("{}/api/stacks/{}/file".format(self.url, self.stack_id), None, self._auth())["StackFileContent"]
		env = [item for item in (stack.get("Env") or []) if item.get("name") != "MMO_TAG"]
		env.append({"name": "MMO_TAG", "value": tag})
		url = "{}/api/stacks/{}?endpointId={}".format(self.url, self.stack_id, self.endpoint_id)
		self.http.send_json("PUT", url, {"stackFileContent": content, "env": env, "prune": False, "pullImage": True}, None, self._auth())

	def service_states(self):
		"""Compose service name -> container state ("running", "exited", ...)."""
		name = self._stack()["Name"]
		filters = json.dumps({"label": ["com.docker.compose.project={}".format(name)]})
		url = "{}/api/endpoints/{}/docker/containers/json?all=1&filters={}".format(
			self.url, self.endpoint_id, urllib.parse.quote(filters))
		containers = self.http.get_json(url, None, self._auth())
		return {c["Labels"].get("com.docker.compose.service", c["Names"][0]): c["State"] for c in containers}


class Notifier:
	"""Discord or Slack webhook. Logs every message; delivery failures are only logged."""

	def __init__(self, url, fmt, http):
		self.url = url
		self.key = "content" if fmt == "discord" else "text"
		self.http = http

	def send(self, text):
		log.info("notify: %s", text)
		if not self.url:
			return
		try:
			self.http.send_json("POST", self.url, {self.key: text[:MESSAGE_LIMIT]})
		except (HttpError, OSError, ValueError) as error:
			log.warning("notification failed: %s", error)
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `python -m unittest discover -s deploy/deployer/tests -t deploy/deployer -p "test_*.py"`
Expected: `OK` (all tests, including 16 new ones).

- [ ] **Step 6: Commit**

```bash
git add deploy/deployer
git commit -m "feat(deployer): GitHub, realm REST, Portainer and webhook clients"
```

---

### Task 7: Patch directory and staging

**Files:**
- Create: `deploy/deployer/mmo_deployer/patchdir.py`, `deploy/deployer/mmo_deployer/stage.py`
- Create: `deploy/deployer/tests/fake_update_compiler.py`
- Modify: `deploy/deployer/tests/fakes.py` (append `CAN_SYMLINK`, `FakeGitHub`, `make_git_repo`, `make_release_assets`)
- Test: `deploy/deployer/tests/test_stage.py`

**Interfaces:**
- Consumes: `Config` (Task 5), `HttpError` (Task 5). The GitHub client's duck type from Task 6: `fetch_manifest(release, dir)`, `download_asset(release, name, dest)`.
- Produces:
  - `PatchDir(root)` with:
    - attributes `.root`, `.releases`
    - `release_path(sha)`, `tmp_path(sha)`, `has_release(sha) -> bool`, `current_sha() -> str|None`
    - `flip_current(sha)`: raises `FileNotFoundError` if the release is missing
    - `prune(keep, protect: set) -> list[str]`
  - `stage.select_release(releases, state) -> dict|None`
  - `stage.StageError`
  - `Stager(cfg, github, patchdir, compiler_command=None, run=subprocess.run)` with `.stage(release) -> manifest dict` (raises `StageError`). It also exposes `.workspace`, `.data_checkout`, `.patch_source`.

- [ ] **Step 1: Append test helpers to `tests/fakes.py`**

Add these imports at the top: `hashlib`, `io`, `os`, `subprocess`, `tempfile`, `zipfile`. Then append:

```python
def _can_symlink():
	with tempfile.TemporaryDirectory() as tmp:
		try:
			os.symlink(tmp, os.path.join(tmp, "link"))
			return True
		except (OSError, NotImplementedError):
			return False


# Windows without developer mode cannot create symlinks; the Linux CI gate runs these tests.
CAN_SYMLINK = _can_symlink()

SOURCE_TXT = (
	'version = 0\nroot = (type = "fs", from = ".", to = "", entries = {\n'
	'\t(type = "fs", from = "mmo_client.exe")\n'
	'\t(type = "fs", from = "Launcher.exe")\n'
	'\t(type = "fs", from = "../../data/client", to = "Data")\n})\n'
)


def git(repo, *args):
	return subprocess.run(
		["git", "-C", str(repo), "-c", "user.name=Test", "-c", "user.email=test@example.com"] + list(args),
		check=True, capture_output=True, text=True).stdout.strip()


def make_git_repo(path, files):
	"""Creates a repo at `path` with one commit holding `files` ({relative path: text}); returns its sha."""
	os.makedirs(path, exist_ok=True)
	git(path, "init", "-q")
	for name, text in files.items():
		full = os.path.join(path, name)
		os.makedirs(os.path.dirname(full), exist_ok=True)
		with open(full, "w", encoding="utf-8") as handle:
			handle.write(text)
	git(path, "add", "-A")
	git(path, "commit", "-q", "-m", "data")
	return git(path, "rev-parse", "HEAD")


def make_release_assets(commit, data_commit, binaries=None, source_txt=SOURCE_TXT, changes=None, sha_override=None):
	"""Returns {asset name: bytes} for a release: client-bin.zip and a matching release.json."""
	binaries = binaries if binaries is not None else {"mmo_client.exe": b"client", "Launcher.exe": b"launcher"}
	buffer = io.BytesIO()
	with zipfile.ZipFile(buffer, "w") as archive:
		if source_txt is not None:
			archive.writestr("source.txt", source_txt)
		for name, data in binaries.items():
			archive.writestr(name, data)
	zip_bytes = buffer.getvalue()
	manifest = {
		"schema": 1,
		"commit": commit,
		"image_tag": commit,
		"data_client_commit": data_commit,
		"data_editor_commit": "0" * 40,
		"created_at": "2026-10-07T00:50:00Z",
		"assets": {"client-bin.zip": {"sha256": sha_override or hashlib.sha256(zip_bytes).hexdigest(), "size": len(zip_bytes)}},
		"gate": {"passed": True, "steps": []},
		"changes": changes if changes is not None else ["fix(loot): roll on the right table"],
	}
	return {"client-bin.zip": zip_bytes, "release.json": json.dumps(manifest).encode("utf-8")}


class FakeGitHub:
	def __init__(self):
		self.releases = []
		self.assets = {}
		self.list_error = None

	def add_release(self, tag, commit, assets, created_at):
		release = {
			"tag_name": tag, "target_commitish": commit, "created_at": created_at, "draft": False,
			"assets": [{"name": name, "id": index} for index, name in enumerate(sorted(assets))],
		}
		for name, data in assets.items():
			self.assets[(tag, name)] = data
		self.releases.append(release)
		return release

	def list_releases(self, prefix):
		if self.list_error:
			raise self.list_error
		matching = [r for r in self.releases if r["tag_name"].startswith(prefix)]
		return sorted(matching, key=lambda r: r["created_at"], reverse=True)

	def download_asset(self, release, name, dest):
		key = (release["tag_name"], name)
		if key not in self.assets:
			raise KeyError(name)
		with open(dest, "wb") as handle:
			handle.write(self.assets[key])

	def fetch_manifest(self, release, scratch_dir):
		return json.loads(self.assets[(release["tag_name"], "release.json")].decode("utf-8"))
```

- [ ] **Step 2: Create `tests/fake_update_compiler.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Stands in for update_compiler in tests: writes a list.txt naming every source file.

FAKE_COMPILER_FAIL=1 makes it fail; FAKE_COMPILER_OMIT=<name> leaves one file out.
"""

import argparse
import os
import sys

parser = argparse.ArgumentParser()
parser.add_argument("-s", required=True)
parser.add_argument("-o", required=True)
parser.add_argument("-c")
parser.add_argument("-j")
args = parser.parse_args()

if os.environ.get("FAKE_COMPILER_FAIL"):
	sys.stderr.write("simulated compiler failure\n")
	sys.exit(3)
data = os.path.join(args.s, "..", "..", "data", "client")
if not os.path.isdir(data):
	sys.stderr.write("data/client missing at {}\n".format(data))
	sys.exit(4)
omit = os.environ.get("FAKE_COMPILER_OMIT", "")
os.makedirs(os.path.join(args.o, "Data"), exist_ok=True)
with open(os.path.join(args.o, "list.txt"), "w", encoding="utf-8") as out:
	out.write("version = 1\n")
	for name in sorted(os.listdir(args.s)):
		if name != omit:
			out.write('name = "{}"\n'.format(name))
```

- [ ] **Step 3: Write the failing tests `tests/test_stage.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

import os
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

from mmo_deployer.patchdir import PatchDir
from mmo_deployer.stage import StageError, Stager, select_release
from mmo_deployer.state import State

from .fakes import CAN_SYMLINK, FakeGitHub, make_config, make_git_repo, make_release_assets

FAKE_COMPILER = [sys.executable, os.path.join(os.path.dirname(__file__), "fake_update_compiler.py")]
COMMIT = "c" * 40


def _releases(*commits):
	return [{"tag_name": "nightly-{}".format(c[:8]), "target_commitish": c, "created_at": "2026-10-0{}".format(i)} for i, c in enumerate(commits)][::-1]


class SelectRelease(unittest.TestCase):
	def test_newest(self):
		self.assertEqual(select_release(_releases("a", "b"), State())["target_commitish"], "b")

	def test_nothing_published(self):
		self.assertIsNone(select_release([], State()))

	def test_newest_already_live_or_staged(self):
		self.assertIsNone(select_release(_releases("a", "b"), State(live="b")))
		self.assertIsNone(select_release(_releases("a", "b"), State(staged="b")))

	def test_bad_newest_release_does_not_fall_back_to_older(self):
		self.assertIsNone(select_release(_releases("a", "b"), State(live="z", bad=["b"])))


class PatchDirTests(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.dir = PatchDir(self.tmp.name)
		for sha in ("one", "two", "three", "four"):
			self.dir.release_path(sha).mkdir(parents=True)
			time.sleep(0.01)
		self.dir.tmp_path("five").mkdir()

	def tearDown(self):
		self.tmp.cleanup()

	def test_prune_keeps_newest_and_protected(self):
		removed = self.dir.prune(2, {"one"})
		self.assertEqual(sorted(removed), ["two"])
		self.assertTrue(self.dir.has_release("one"))
		self.assertTrue(self.dir.tmp_path("five").exists())

	def test_flip_missing_release(self):
		with self.assertRaises(FileNotFoundError):
			self.dir.flip_current("nope")

	def test_no_current_link(self):
		self.assertIsNone(self.dir.current_sha())

	@unittest.skipUnless(CAN_SYMLINK, "needs symlink support")
	def test_flip_is_relative_and_replaces(self):
		self.dir.flip_current("one")
		self.dir.flip_current("two")
		self.assertEqual(self.dir.current_sha(), "two")
		self.assertEqual(os.readlink(os.path.join(self.tmp.name, "current")), os.path.join("releases", "two"))
		self.assertFalse(os.path.lexists(os.path.join(self.tmp.name, "current.new")))


class StagerTests(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		root = Path(self.tmp.name)
		self.data_commit = make_git_repo(root / "mmo-data", {"Worlds/map.txt": "terrain"})
		self.cfg = make_config(root, data_repo_url=str(root / "mmo-data"))
		self.github = FakeGitHub()
		self.patchdir = PatchDir(self.cfg.patch_root)
		self.stager = Stager(self.cfg, self.github, self.patchdir, FAKE_COMPILER)

	def tearDown(self):
		self.tmp.cleanup()

	def _release(self, **kwargs):
		assets = make_release_assets(kwargs.pop("commit", COMMIT), kwargs.pop("data_commit", self.data_commit), **kwargs)
		return self.github.add_release("nightly-20261007-cccccccc", COMMIT, assets, "2026-10-07T00:50:00Z")

	def _assert_nothing_left(self):
		self.assertFalse(self.patchdir.has_release(COMMIT))
		self.assertFalse(self.patchdir.tmp_path(COMMIT).exists())

	def test_stage_builds_release(self):
		manifest = self.stager.stage(self._release())
		self.assertEqual(manifest["commit"], COMMIT)
		listing = (self.patchdir.release_path(COMMIT) / "list.txt").read_text(encoding="utf-8")
		self.assertIn('"mmo_client.exe"', listing)
		self.assertIn('"source.txt"', listing)
		self.assertFalse(self.patchdir.tmp_path(COMMIT).exists())
		self.assertTrue((self.stager.data_checkout / "Worlds" / "map.txt").is_file())

	def test_restage_replaces_existing_release(self):
		self.stager.stage(self._release())
		marker = self.patchdir.release_path(COMMIT) / "stale"
		marker.write_text("x")
		self.stager.stage(self.github.releases[0])
		self.assertFalse(marker.exists())

	def test_checksum_mismatch(self):
		with self.assertRaises(StageError) as ctx:
			self.stager.stage(self._release(sha_override="0" * 64))
		self.assertIn("checksum", str(ctx.exception))
		self._assert_nothing_left()

	def test_manifest_commit_mismatch(self):
		with self.assertRaises(StageError):
			self.stager.stage(self._release(commit="d" * 40))
		self._assert_nothing_left()

	def test_compiler_failure(self):
		with mock.patch.dict(os.environ, {"FAKE_COMPILER_FAIL": "1"}):
			with self.assertRaises(StageError) as ctx:
				self.stager.stage(self._release())
		self.assertIn("simulated compiler failure", str(ctx.exception))
		self._assert_nothing_left()

	def test_list_missing_a_binary(self):
		with mock.patch.dict(os.environ, {"FAKE_COMPILER_OMIT": "Launcher.exe"}):
			with self.assertRaises(StageError) as ctx:
				self.stager.stage(self._release())
		self.assertIn("Launcher.exe", str(ctx.exception))
		self._assert_nothing_left()

	def test_unknown_data_commit_fails_cleanly(self):
		with self.assertRaises(StageError) as ctx:
			self.stager.stage(self._release(data_commit="e" * 40))
		self.assertIn("git", str(ctx.exception))
		self._assert_nothing_left()

	def test_zip_without_source_txt(self):
		with self.assertRaises(StageError):
			self.stager.stage(self._release(source_txt=None))
		self._assert_nothing_left()


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 4: Run the tests to verify they fail**

Run: `python -m unittest discover -s deploy/deployer/tests -t deploy/deployer -p "test_stage.py"`
Expected: FAIL with `ModuleNotFoundError: No module named 'mmo_deployer.patchdir'`.

- [ ] **Step 5: Implement `mmo_deployer/patchdir.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""The patch root served by nginx: releases/<sha>/ and a `current` symlink to one of them."""

import os
import shutil
from pathlib import Path


class PatchDir:
	def __init__(self, root):
		self.root = Path(root)
		self.releases = self.root / "releases"

	def release_path(self, sha):
		return self.releases / sha

	def tmp_path(self, sha):
		return self.releases / (sha + ".tmp")

	def has_release(self, sha):
		return bool(sha) and self.release_path(sha).is_dir()

	def current_sha(self):
		link = self.root / "current"
		if not link.is_symlink():
			return None
		return Path(os.readlink(link)).name

	def flip_current(self, sha):
		"""Atomically points `current` at releases/<sha>."""
		if not self.has_release(sha):
			raise FileNotFoundError(str(self.release_path(sha)))
		staging = self.root / "current.new"
		if os.path.lexists(staging):
			staging.unlink()
		# Relative, so the link resolves the same on the host (nginx) and in the container.
		os.symlink(os.path.join("releases", sha), staging)
		os.replace(staging, self.root / "current")

	def prune(self, keep, protect):
		"""Deletes all but the `keep` newest releases, never one in `protect`. Returns removed shas."""
		if not self.releases.is_dir():
			return []
		releases = [p for p in self.releases.iterdir() if p.is_dir() and not p.name.endswith(".tmp")]
		releases.sort(key=lambda p: p.stat().st_mtime, reverse=True)
		removed = []
		for path in releases[keep:]:
			if path.name in protect:
				continue
			shutil.rmtree(path)
			removed.append(path.name)
		return removed
```

- [ ] **Step 6: Implement `mmo_deployer/stage.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Turns a nightly GitHub release into releases/<sha>/ on the patch root."""

import hashlib
import logging
import os
import shutil
import subprocess
import zipfile
from pathlib import Path

from .web import HttpError

log = logging.getLogger("deployer")


class StageError(Exception):
	pass


def select_release(releases, state):
	"""The newest release, unless it is live, staged or bad.

	Only the newest is considered: falling back to an older one could go backwards.
	"""
	if not releases:
		return None
	newest = releases[0]
	commit = newest["target_commitish"]
	if commit in (state.live, state.staged) or commit in state.bad:
		return None
	return newest


def sha256_file(path):
	digest = hashlib.sha256()
	with open(path, "rb") as handle:
		for chunk in iter(lambda: handle.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


class Stager:
	def __init__(self, cfg, github, patchdir, compiler_command=None, run=subprocess.run):
		self.cfg = cfg
		self.github = github
		self.patchdir = patchdir
		self.compiler_command = compiler_command or [cfg.update_compiler]
		self.run = run
		# The workspace mirrors the repo layout, so source.txt's ../../data/client resolves.
		self.workspace = Path(cfg.state_dir) / "workspace"
		self.data_checkout = self.workspace / "data" / "client"
		self.patch_source = self.workspace / "deploy" / "patch"
		self.downloads = Path(cfg.state_dir) / "downloads"

	def stage(self, release):
		"""Builds releases/<sha>/ for `release` and returns its release.json. Raises StageError."""
		commit = release["target_commitish"]
		tmp = self.patchdir.tmp_path(commit)
		try:
			return self._stage(release, commit, tmp)
		except StageError:
			raise
		except (OSError, KeyError, ValueError, HttpError, zipfile.BadZipFile) as error:
			raise StageError("{}: {}".format(type(error).__name__, error)) from error
		finally:
			if tmp.exists():
				shutil.rmtree(tmp, ignore_errors=True)

	def _stage(self, release, commit, tmp):
		self.downloads.mkdir(parents=True, exist_ok=True)
		manifest = self.github.fetch_manifest(release, str(self.downloads))
		if manifest.get("schema") != 1:
			raise StageError("unsupported release.json schema {}".format(manifest.get("schema")))
		if manifest["commit"] != commit:
			raise StageError("release.json names {} but the release targets {}".format(manifest["commit"], commit))

		zip_path = self.downloads / "client-bin.zip"
		self.github.download_asset(release, "client-bin.zip", str(zip_path))
		if sha256_file(zip_path) != manifest["assets"]["client-bin.zip"]["sha256"]:
			raise StageError("client-bin.zip checksum mismatch")

		self._checkout_data(manifest["data_client_commit"])

		if self.patch_source.exists():
			shutil.rmtree(self.patch_source)
		self.patch_source.mkdir(parents=True)
		with zipfile.ZipFile(zip_path) as archive:
			archive.extractall(self.patch_source)
			binaries = [name for name in archive.namelist() if name != "source.txt"]
		if not (self.patch_source / "source.txt").is_file():
			raise StageError("client-bin.zip contains no source.txt")

		if tmp.exists():
			shutil.rmtree(tmp)
		self.patchdir.releases.mkdir(parents=True, exist_ok=True)
		self._run(self.compiler_command + [
			"-s", str(self.patch_source), "-o", str(tmp), "-c", "zlib", "-j", str(self.cfg.compiler_threads)])
		self._sanity_check(tmp, binaries)

		final = self.patchdir.release_path(commit)
		if final.exists():
			shutil.rmtree(final)
		os.replace(tmp, final)
		log.info("staged %s into %s", commit, final)
		return manifest

	def _checkout_data(self, data_commit):
		if not (self.data_checkout / ".git").exists():
			self.data_checkout.parent.mkdir(parents=True, exist_ok=True)
			self._run(["git", "clone", "--quiet", "--no-checkout", self.cfg.data_repo_url, str(self.data_checkout)])
		git = ["git", "-C", str(self.data_checkout)]
		self._run(git + ["fetch", "--quiet", "origin", data_commit])
		self._run(git + ["checkout", "--quiet", "--force", data_commit])
		self._run(git + ["clean", "-ffdxq"])

	def _sanity_check(self, output, binaries):
		listing = output / "list.txt"
		if not listing.is_file() or listing.stat().st_size == 0:
			raise StageError("update_compiler produced no list.txt")
		text = listing.read_text(encoding="utf-8", errors="replace")
		for name in binaries:
			if name not in text:
				raise StageError("list.txt does not mention {}".format(name))

	def _run(self, command):
		result = self.run(command, capture_output=True, text=True)
		if result.returncode != 0:
			detail = (result.stderr or result.stdout or "").strip()[-2000:]
			raise StageError("{} failed ({}): {}".format(" ".join(command[:3]), result.returncode, detail))
```

The `git` subcommand name is part of every git failure message. That is what `test_unknown_data_commit_fails_cleanly` asserts on: the message starts with `git -C <path>`.

- [ ] **Step 7: Run the tests to verify they pass**

Run: `python -m unittest discover -s deploy/deployer/tests -t deploy/deployer -p "test_*.py"`
Expected: `OK`. The symlink test may show as skipped on Windows; it runs in the Linux CI gate.

- [ ] **Step 8: Commit**

```bash
git add deploy/deployer
git commit -m "feat(deployer): stage nightly releases into the patch root"
```

---

## Phase 3 — Deployer: maintenance

### Task 8: Database backup and launcher patch notes

**Files:**
- Create: `deploy/deployer/mmo_deployer/backup.py`, `deploy/deployer/mmo_deployer/news.py`
- Test: `deploy/deployer/tests/test_backup.py`, `deploy/deployer/tests/test_news.py`

**Interfaces:**
- Produces:
  - `backup.BackupError`
  - `backup.dump_databases(cfg, dest: Path, run=subprocess.run) -> list[Path]`
  - `backup.prune_backups(root: Path, keep) -> list[str]`
  - `news.add_patch_note(path, date_text, changes) -> bool` (False if `launcher.json` is missing)
  - `news.describe_change(subject) -> str`
  - Constants `news.MANIFEST_LIMIT = 256 * 1024`, `news.MAX_PATCHES = 32`

- [ ] **Step 1: Write the failing tests**

`tests/test_backup.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

import gzip
import subprocess
import tempfile
import unittest
from pathlib import Path

from mmo_deployer.backup import BackupError, dump_databases, prune_backups

from .fakes import make_config


class FakeRun:
	def __init__(self, returncode=0, stdout=b"-- dump", stderr=b""):
		self.calls = []
		self.result = subprocess.CompletedProcess([], returncode, stdout, stderr)

	def __call__(self, command, **kwargs):
		self.calls.append((command, kwargs))
		return self.result


class Backup(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.cfg = make_config(self.tmp.name)
		self.dest = Path(self.tmp.name) / "backups" / "20261007-044500-cccccccc"

	def tearDown(self):
		self.tmp.cleanup()

	def test_dumps_every_database_compressed(self):
		run = FakeRun()
		paths = dump_databases(self.cfg, self.dest, run)
		self.assertEqual([p.name for p in paths], ["mmo_login.sql.gz", "mmo_realm_01.sql.gz"])
		with gzip.open(paths[0]) as handle:
			self.assertEqual(handle.read(), b"-- dump")
		command, kwargs = run.calls[0]
		self.assertEqual(command[0], "mysqldump")
		self.assertIn("--single-transaction", command)
		self.assertEqual(command[-1], "mmo_login")
		self.assertEqual(kwargs["env"]["MYSQL_PWD"], "secret")
		self.assertNotIn("secret", command)

	def test_failure_raises(self):
		with self.assertRaises(BackupError) as ctx:
			dump_databases(self.cfg, self.dest, FakeRun(returncode=2, stderr=b"access denied"))
		self.assertIn("access denied", str(ctx.exception))

	def test_empty_dump_raises(self):
		with self.assertRaises(BackupError):
			dump_databases(self.cfg, self.dest, FakeRun(stdout=b""))

	def test_prune_keeps_newest_by_name(self):
		root = Path(self.tmp.name) / "backups"
		for name in ("20261001-0445-a", "20261002-0445-b", "20261003-0445-c"):
			(root / name).mkdir(parents=True)
		self.assertEqual(prune_backups(root, 2), ["20261001-0445-a"])
		self.assertEqual(sorted(p.name for p in root.iterdir()), ["20261002-0445-b", "20261003-0445-c"])

	def test_prune_missing_dir(self):
		self.assertEqual(prune_backups(Path(self.tmp.name) / "none", 3), [])


if __name__ == "__main__":
	unittest.main()
```

`tests/test_news.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

import json
import tempfile
import unittest
from pathlib import Path

from mmo_deployer.news import MANIFEST_LIMIT, MAX_PATCHES, add_patch_note, describe_change


class News(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.path = Path(self.tmp.name) / "launcher.json"
		self.path.write_text(json.dumps({"version": 1, "news": [], "patches": [{"title": "Old", "summary": "s", "body": "b"}]}), encoding="utf-8")

	def tearDown(self):
		self.tmp.cleanup()

	def _load(self):
		return json.loads(self.path.read_text(encoding="utf-8"))

	def test_describe_change_strips_conventional_prefix(self):
		self.assertEqual(describe_change("fix(loot): roll on the right table"), "Roll on the right table")
		self.assertEqual(describe_change("feat!: breaking"), "Breaking")
		self.assertEqual(describe_change("Plain subject"), "Plain subject")

	def test_prepends_entry(self):
		self.assertTrue(add_patch_note(self.path, "2026-10-07", ["fix(loot): roll on the right table", "feat(chat): emotes"]))
		patches = self._load()["patches"]
		self.assertEqual(patches[0]["title"], "Update 2026-10-07")
		self.assertEqual(patches[0]["summary"], "2 fixes and improvements")
		self.assertIn("- Roll on the right table", patches[0]["body"])
		self.assertEqual(patches[1]["title"], "Old")
		self.assertEqual(self._load()["version"], 1)

	def test_no_changes(self):
		add_patch_note(self.path, "2026-10-07", [])
		self.assertEqual(self._load()["patches"][0]["summary"], "Maintenance update")

	def test_missing_file(self):
		self.assertFalse(add_patch_note(Path(self.tmp.name) / "none.json", "2026-10-07", ["x"]))

	def test_manifest_stays_within_launcher_limits(self):
		big = ["fix: " + "x" * 300] * 100
		for day in range(1, 41):
			add_patch_note(self.path, "2026-11-{:02d}".format(day), big)
		data = self._load()
		self.assertLessEqual(len(data["patches"]), MAX_PATCHES)
		self.assertLessEqual(len(self.path.read_bytes()), MANIFEST_LIMIT)
		self.assertEqual(data["patches"][0]["title"], "Update 2026-11-40")
		for entry in data["patches"]:
			self.assertLessEqual(len(entry["summary"].encode("utf-8")), 400)


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python -m unittest discover -s deploy/deployer/tests -t deploy/deployer -p "test_[bn]*.py"`
Expected: FAIL with `ModuleNotFoundError` for `mmo_deployer.backup` and `mmo_deployer.news`.

- [ ] **Step 3: Implement `mmo_deployer/backup.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""mysqldump of the login/realm databases before every redeploy.

Migrations apply themselves when the new servers start; this is their only undo.
"""

import gzip
import os
import shutil
import subprocess
from pathlib import Path


class BackupError(Exception):
	pass


def dump_databases(cfg, dest, run=subprocess.run):
	dest = Path(dest)
	dest.mkdir(parents=True, exist_ok=True)
	env = dict(os.environ, MYSQL_PWD=cfg.mysql_password)
	paths = []
	for database in cfg.backup_databases:
		result = run(
			["mysqldump", "--single-transaction", "--routines", "--triggers",
				"-h", cfg.mysql_host, "-u", cfg.mysql_user, database],
			capture_output=True, env=env)
		if result.returncode != 0:
			raise BackupError("mysqldump {} failed: {}".format(database, result.stderr.decode("utf-8", "replace")[-1000:]))
		if not result.stdout:
			raise BackupError("mysqldump {} produced no output".format(database))
		target = dest / (database + ".sql.gz")
		with gzip.open(target, "wb") as out:
			out.write(result.stdout)
		paths.append(target)
	return paths


def prune_backups(root, keep):
	"""Backup folders are named <timestamp>-<sha8>, so name order is age order."""
	root = Path(root)
	if not root.is_dir():
		return []
	folders = sorted((p for p in root.iterdir() if p.is_dir()), key=lambda p: p.name, reverse=True)
	removed = []
	for path in folders[keep:]:
		shutil.rmtree(path)
		removed.append(path.name)
	return removed
```

- [ ] **Step 4: Implement `mmo_deployer/news.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Prepends nightly patch notes to the launcher's launcher.json.

Limits come from docs/launcher_content/README.md.
"""

import json
import os
import re
from pathlib import Path

MANIFEST_LIMIT = 256 * 1024
MAX_PATCHES = 32
TITLE_LIMIT = 180
DATE_LIMIT = 80
SUMMARY_LIMIT = 400
# Far below the launcher's 64 KiB per body, so 32 entries fit the 256 KiB manifest.
BODY_LIMIT = 6 * 1024
MAX_LINES = 100

_PREFIX = re.compile(r"^\w+(\([^)]*\))?!?:\s*")


def _clip(text, limit):
	encoded = text.encode("utf-8")
	if len(encoded) <= limit:
		return text
	return encoded[:limit - 3].decode("utf-8", "ignore") + "..."


def _encode(content):
	return json.dumps(content, indent=2, ensure_ascii=False).encode("utf-8")


def describe_change(subject):
	stripped = _PREFIX.sub("", subject).strip()
	if not stripped:
		return subject
	return stripped[:1].upper() + stripped[1:]


def add_patch_note(path, date_text, changes):
	path = Path(path)
	if not path.is_file():
		return False
	content = json.loads(path.read_text(encoding="utf-8"))
	lines = [describe_change(change) for change in changes][:MAX_LINES]
	if not lines:
		summary = "Maintenance update"
		body = "## Changes\n- Maintenance update"
	else:
		summary = lines[0] if len(lines) == 1 else "{} fixes and improvements".format(len(lines))
		body = "## Changes\n" + "\n".join("- " + line for line in lines)
	entry = {
		"title": _clip("Update " + date_text, TITLE_LIMIT),
		"date": _clip(date_text, DATE_LIMIT),
		"summary": _clip(summary, SUMMARY_LIMIT),
		"body": _clip(body, BODY_LIMIT),
	}
	patches = ([entry] + list(content.get("patches") or []))[:MAX_PATCHES]
	content["patches"] = patches
	while len(patches) > 1 and len(_encode(content)) > MANIFEST_LIMIT:
		patches.pop()
	tmp = path.with_name(path.name + ".tmp")
	tmp.write_bytes(_encode(content))
	os.replace(tmp, path)
	return True
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `python -m unittest discover -s deploy/deployer/tests -t deploy/deployer -p "test_*.py"`
Expected: `OK`.

- [ ] **Step 6: Commit**

```bash
git add deploy/deployer
git commit -m "feat(deployer): database backups and launcher patch notes"
```

---

### Task 9: Maintenance orchestration

**Files:**
- Create: `deploy/deployer/mmo_deployer/maintenance.py`
- Modify: `deploy/deployer/tests/fakes.py` (append `FakeApi`, `FakePortainer`, `FakeNotifier`, `FakePatchDir`)
- Test: `deploy/deployer/tests/test_maintenance.py`

**Interfaces:**
- Consumes: duck types matching Task 6 (`RestApi`: `name`, `uptime`, `schedule_shutdown`, `cancel_shutdown`; `PortainerClient`: `ping`, `current_tag`, `redeploy`, `service_states`; `Notifier.send`) and Task 7 (`PatchDir.has_release`, `flip_current`). `BackupError` from Task 8.
- Produces:
  - `Outcome` constants: `NOTHING`, `DRY_RUN`, `DEPLOYED`, `ABORTED`, `ROLLED_BACK`, `FAILED`
  - `Maintenance(cfg, realms, login, portainer, patchdir, backup, notifier, clock)`, where `backup` is a callable `(sha) -> None` that raises `BackupError`/`OSError`
  - `.run(state) -> Outcome`: reads only `state.staged` and `state.live`, never mutates `state`
  - `.rollback_to(sha) -> bool`
  - Module constants `VERIFY_TIMEOUT_S = 300`, `EXIT_GRACE_S = 1800`, `POLL_S = 15`, `HEALTHY_ROUNDS = 2`

- [ ] **Step 1: Append fakes to `tests/fakes.py`**

```python
class FakeApi:
	def __init__(self, name, health=None, schedule_error=None):
		self.name = name
		self.health = health or (lambda: True)
		self.schedule_error = schedule_error
		self.calls = []

	def uptime(self):
		self.calls.append("uptime")
		if not self.health():
			raise OSError("connection refused")
		return 10

	def schedule_shutdown(self, delay):
		self.calls.append(("shutdown", delay))
		if self.schedule_error:
			raise self.schedule_error
		return True

	def cancel_shutdown(self):
		self.calls.append("cancel")


class FakePortainer:
	def __init__(self, tag="old"):
		self.tag = tag
		self.deploys = []
		self.states = {"realm_server_01": "exited", "world_node_01": "exited"}
		self.ping_error = None
		self.redeploy_error = None

	def ping(self):
		if self.ping_error:
			raise self.ping_error

	def current_tag(self):
		return self.tag

	def redeploy(self, tag):
		self.deploys.append(tag)
		if self.redeploy_error:
			raise self.redeploy_error
		self.tag = tag

	def service_states(self):
		return dict(self.states)


class FakeNotifier:
	def __init__(self):
		self.messages = []

	def send(self, text):
		self.messages.append(text)


class FakePatchDir:
	def __init__(self, root, releases, current=None):
		self.root = Path(root)
		self.release_set = set(releases)
		self.current = current
		self.flips = []
		self.pruned = []

	def has_release(self, sha):
		return sha in self.release_set

	def flip_current(self, sha):
		if sha not in self.release_set:
			raise FileNotFoundError(sha)
		self.flips.append(sha)
		self.current = sha

	def prune(self, keep, protect):
		self.pruned.append((keep, set(protect)))
		return []
```

- [ ] **Step 2: Write the failing tests `tests/test_maintenance.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

import datetime
import itertools
import tempfile
import unittest

from mmo_deployer.backup import BackupError
from mmo_deployer.maintenance import EXIT_GRACE_S, VERIFY_TIMEOUT_S, Maintenance, Outcome
from mmo_deployer.state import State
from mmo_deployer.web import HttpError

from .fakes import FakeApi, FakeClock, FakeNotifier, FakePatchDir, FakePortainer, make_config


class MaintenanceTests(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.cfg = make_config(self.tmp.name)
		self.clock = FakeClock(datetime.datetime(2026, 10, 7, 4, 45, tzinfo=datetime.timezone.utc))
		self.portainer = FakePortainer("old")
		self.realm_a = FakeApi("realm-a")
		self.realm_b = FakeApi("realm-b")
		self.login = FakeApi("login")
		self.patchdir = FakePatchDir(self.tmp.name, {"old", "new"}, current="old")
		self.backups = []
		self.notifier = FakeNotifier()
		self.state = State(live="old", staged="new")

	def tearDown(self):
		self.tmp.cleanup()

	def make(self, backup=None, cfg=None):
		return Maintenance(cfg or self.cfg, [self.realm_a, self.realm_b], self.login, self.portainer,
			self.patchdir, backup or self.backups.append, self.notifier, self.clock)

	def test_nothing_staged(self):
		self.assertEqual(self.make().run(State(live="old")), Outcome.NOTHING)
		self.assertEqual(self.make().run(State(live="old", staged="old")), Outcome.NOTHING)
		self.assertEqual(self.realm_a.calls, [])

	def test_happy_path(self):
		outcome = self.make().run(self.state)
		self.assertEqual(outcome, Outcome.DEPLOYED)
		self.assertIn(("shutdown", 900), self.realm_a.calls)
		self.assertIn(("shutdown", 900), self.realm_b.calls)
		self.assertEqual(self.backups, ["new"])
		self.assertEqual(self.patchdir.flips, ["new"])
		self.assertEqual(self.portainer.deploys, ["new"])
		self.assertTrue(any("Maintenance started" in m for m in self.notifier.messages))
		self.assertEqual(self.state, State(live="old", staged="new"))

	def test_preflight_realm_down_aborts_before_shutdown(self):
		self.realm_b.health = lambda: False
		self.assertEqual(self.make().run(self.state), Outcome.ABORTED)
		self.assertNotIn(("shutdown", 900), self.realm_a.calls)
		self.assertEqual(self.portainer.deploys, [])
		self.assertTrue(any("realm-b" in m for m in self.notifier.messages))

	def test_preflight_portainer_down_aborts(self):
		self.portainer.ping_error = OSError("refused")
		self.assertEqual(self.make().run(self.state), Outcome.ABORTED)
		self.assertNotIn(("shutdown", 900), self.realm_a.calls)

	def test_preflight_missing_release_aborts(self):
		self.patchdir.release_set.discard("new")
		self.assertEqual(self.make().run(self.state), Outcome.ABORTED)

	def test_partial_schedule_failure_cancels_accepted_realms(self):
		self.realm_b.schedule_error = HttpError(503, "unavailable", "x")
		self.assertEqual(self.make().run(self.state), Outcome.ABORTED)
		self.assertIn("cancel", self.realm_a.calls)
		self.assertEqual(self.portainer.deploys, [])
		self.assertEqual(self.patchdir.flips, [])

	def test_dry_run_changes_nothing(self):
		cfg = make_config(self.tmp.name, dry_run=True)
		self.assertEqual(self.make(cfg=cfg).run(self.state), Outcome.DRY_RUN)
		self.assertNotIn(("shutdown", 900), self.realm_a.calls)
		self.assertEqual(self.portainer.deploys, [])
		self.assertTrue(any("dry-run" in m for m in self.notifier.messages))

	def test_containers_that_never_exit_do_not_block_forever(self):
		self.portainer.states = {"realm_server_01": "running", "world_node_01": "running"}
		self.assertEqual(self.make().run(self.state), Outcome.DEPLOYED)
		self.assertGreaterEqual(self.clock.slept, self.cfg.countdown_s + EXIT_GRACE_S)

	def test_backup_failure_restarts_current_release(self):
		def failing_backup(sha):
			raise BackupError("disk full")
		self.assertEqual(self.make(backup=failing_backup).run(self.state), Outcome.ABORTED)
		self.assertEqual(self.patchdir.flips, [])
		self.assertEqual(self.portainer.deploys, ["old"])
		self.assertTrue(any("disk full" in m for m in self.notifier.messages))

	def test_failed_verification_rolls_back(self):
		self.realm_a.health = lambda: self.portainer.tag != "new"
		self.assertEqual(self.make().run(self.state), Outcome.ROLLED_BACK)
		self.assertEqual(self.patchdir.flips, ["new", "old"])
		self.assertEqual(self.portainer.deploys, ["new", "old"])

	def test_crash_looping_realm_is_rolled_back(self):
		flaky = itertools.cycle([True, False])
		self.realm_a.health = lambda: self.portainer.tag != "new" or next(flaky)
		self.assertEqual(self.make().run(self.state), Outcome.ROLLED_BACK)
		self.assertEqual(self.portainer.deploys, ["new", "old"])

	def test_redeploy_error_attempts_rollback(self):
		self.portainer.redeploy_error = OSError("timeout")
		self.assertEqual(self.make().run(self.state), Outcome.FAILED)
		self.assertEqual(self.portainer.deploys, ["new", "old"])

	def test_no_previous_release_means_failed(self):
		self.realm_a.health = lambda: self.portainer.tag != "new"
		self.assertEqual(self.make().run(State(live=None, staged="new")), Outcome.FAILED)

	def test_rollback_that_also_fails(self):
		# Healthy for the preflight; unhealthy after any redeploy, including the rollback.
		self.realm_a.health = lambda: not self.portainer.deploys
		self.assertEqual(self.make().run(self.state), Outcome.FAILED)

	def test_verification_gives_up_after_timeout(self):
		self.realm_a.health = lambda: self.portainer.tag != "new"
		start = self.clock.now()
		self.make().run(self.state)
		self.assertGreaterEqual((self.clock.now() - start).total_seconds(), VERIFY_TIMEOUT_S)


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `python -m unittest discover -s deploy/deployer/tests -t deploy/deployer -p "test_maintenance.py"`
Expected: FAIL with `ModuleNotFoundError: No module named 'mmo_deployer.maintenance'`.

- [ ] **Step 4: Implement `mmo_deployer/maintenance.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""One maintenance run: preflight → shutdown → backup → flip → redeploy → verify → rollback.

Anything that fails before the shutdown leaves the live release untouched; anything that
fails after it rolls back to the previous release.
"""

import datetime
import logging

from .backup import BackupError
from .web import HttpError

log = logging.getLogger("deployer")

VERIFY_TIMEOUT_S = 300
EXIT_GRACE_S = 1800
POLL_S = 15
# A crash-looping server can answer once between restarts.
HEALTHY_ROUNDS = 2

_REMOTE_ERRORS = (HttpError, OSError, ValueError, KeyError)


class Outcome:
	NOTHING = "nothing"
	DRY_RUN = "dry_run"
	DEPLOYED = "deployed"
	ABORTED = "aborted"
	ROLLED_BACK = "rolled_back"
	FAILED = "failed"


class Abort(Exception):
	"""Stopped before anything changed on the live stack."""


def _short(sha):
	return sha[:8] if sha else "none"


class Maintenance:
	def __init__(self, cfg, realms, login, portainer, patchdir, backup, notifier, clock):
		self.cfg = cfg
		self.realms = list(realms)
		self.login = login
		self.portainer = portainer
		self.patchdir = patchdir
		self.backup = backup
		self.notifier = notifier
		self.clock = clock

	def run(self, state):
		sha = state.staged
		if not sha or sha == state.live:
			return Outcome.NOTHING
		try:
			self._preflight(sha)
		except Abort as error:
			self.notifier.send("Maintenance for {} aborted, nothing changed: {}".format(_short(sha), error))
			return Outcome.ABORTED

		if self.cfg.dry_run:
			self.notifier.send("[dry-run] would deploy {} after a {} s countdown on {}".format(
				_short(sha), self.cfg.countdown_s, ", ".join(r.name for r in self.realms)))
			return Outcome.DRY_RUN

		try:
			self._schedule_shutdowns()
		except Abort as error:
			self.notifier.send("Maintenance for {} aborted, nothing changed: {}".format(_short(sha), error))
			return Outcome.ABORTED
		self.notifier.send("Maintenance started: {} goes live after a {} min countdown.".format(_short(sha), self.cfg.countdown_s // 60))

		self._wait_for_exit()

		try:
			self.backup(sha)
		except (BackupError, OSError) as error:
			tag = self.portainer.current_tag()
			self.notifier.send("Database backup failed ({}); restarting the stack on the current release {}.".format(error, tag))
			try:
				self.portainer.redeploy(tag)
			except _REMOTE_ERRORS as restart_error:
				self.notifier.send("MANUAL INTERVENTION REQUIRED: restart on {} failed: {}".format(tag, restart_error))
			return Outcome.ABORTED

		try:
			self.patchdir.flip_current(sha)
			self.portainer.redeploy(sha)
			healthy = self._verify()
		except _REMOTE_ERRORS as error:
			log.error("deploying %s failed: %s", sha, error)
			healthy = False
		if healthy:
			return Outcome.DEPLOYED

		self.notifier.send("Release {} failed its health check; rolling back to {}.".format(_short(sha), _short(state.live)))
		return Outcome.ROLLED_BACK if self.rollback_to(state.live) else Outcome.FAILED

	def rollback_to(self, sha):
		"""Points patch and stack back at `sha`. True once it is healthy again."""
		if not self.patchdir.has_release(sha):
			log.error("cannot roll back: release %s is not on disk", sha)
			return False
		try:
			self.patchdir.flip_current(sha)
			self.portainer.redeploy(sha)
		except _REMOTE_ERRORS as error:
			log.error("rollback to %s failed: %s", sha, error)
			return False
		return self._verify()

	def _preflight(self, sha):
		if not self.patchdir.has_release(sha):
			raise Abort("staged release {} is missing on disk".format(_short(sha)))
		for api in [self.login] + self.realms:
			try:
				api.uptime()
			except _REMOTE_ERRORS as error:
				raise Abort("{} is not answering: {}".format(api.name, error))
		try:
			self.portainer.ping()
		except _REMOTE_ERRORS as error:
			raise Abort("Portainer is not answering: {}".format(error))

	def _schedule_shutdowns(self):
		accepted = []
		for realm in self.realms:
			try:
				realm.schedule_shutdown(self.cfg.countdown_s)
				accepted.append(realm)
			except _REMOTE_ERRORS as error:
				for other in accepted:
					try:
						other.cancel_shutdown()
					except _REMOTE_ERRORS as cancel_error:
						log.error("could not cancel the shutdown on %s: %s", other.name, cancel_error)
				raise Abort("could not schedule the shutdown on {}: {}".format(realm.name, error))

	def _wait_for_exit(self):
		deadline = self.clock.now() + datetime.timedelta(seconds=self.cfg.countdown_s + EXIT_GRACE_S)
		while self.clock.now() < deadline:
			try:
				states = self.portainer.service_states()
				if all(states.get(service) != "running" for service in self.cfg.wait_services):
					return True
			except _REMOTE_ERRORS as error:
				log.warning("could not read container states: %s", error)
			self.clock.sleep(POLL_S)
		# The stack update sends SIGTERM, which the realm treats as a graceful immediate shutdown.
		log.warning("servers still running after the countdown; the redeploy will stop them")
		return False

	def _healthy(self):
		for api in [self.login] + self.realms:
			try:
				api.uptime()
			except _REMOTE_ERRORS:
				return False
		return True

	def _verify(self):
		deadline = self.clock.now() + datetime.timedelta(seconds=VERIFY_TIMEOUT_S)
		streak = 0
		while True:
			streak = streak + 1 if self._healthy() else 0
			if streak >= HEALTHY_ROUNDS:
				return True
			if self.clock.now() >= deadline:
				return False
			self.clock.sleep(POLL_S)
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `python -m unittest discover -s deploy/deployer/tests -t deploy/deployer -p "test_*.py"`
Expected: `OK`.

- [ ] **Step 6: Commit**

```bash
git add deploy/deployer
git commit -m "feat(deployer): maintenance orchestration with automatic rollback"
```

---

### Task 10: Deployer loop, request queue and CLI

**Files:**
- Create: `deploy/deployer/mmo_deployer/app.py`, `deploy/deployer/mmo_deployer/cli.py`, `deploy/deployer/mmo_deployer/__main__.py`, `deploy/deployer/deployer.sh`
- Modify: `deploy/deployer/tests/fakes.py` (append `FakeStager`, `FakeMaintenance`)
- Test: `deploy/deployer/tests/test_app.py`

**Interfaces:**
- Consumes: everything from Tasks 5-9.
- Produces:
  - `app.Deployer(cfg, github, stager, maintenance, patchdir, notifier, clock)` with `tick()`, `run_forever()`, `maintenance_due(state, now) -> bool`, `run_maintenance(state) -> Outcome`
  - `app.TICK_S = 10`, `app.MAINTENANCE_WINDOW_S = 3600`
  - `app.write_request(state_dir, command, args)`
  - `cli.main(argv=None, env=None) -> int`
  - `cli.build_deployer(cfg, clock=None, backup=None, compiler_command=None, github_api="https://api.github.com") -> Deployer`
  - CLI commands: `run`, `status`, `pause`, `resume`, `stage-now`, `deploy-now [sha]`, `rollback`, `unbad <sha>`. All mutating commands go through `state_dir/requests/*.json`; only the loop process writes `state.json`.

- [ ] **Step 1: Append fakes to `tests/fakes.py`**

```python
class FakeStager:
	def __init__(self):
		self.staged = []
		self.error = None
		self.manifests = {}

	def stage(self, release):
		self.staged.append(release["target_commitish"])
		if self.error:
			raise self.error
		return self.manifests.get(release["target_commitish"], {"changes": ["fix(loot): roll on the right table"]})


class FakeMaintenance:
	def __init__(self, outcome="deployed", rollback_ok=True):
		self.outcome = outcome
		self.rollback_ok = rollback_ok
		self.runs = []
		self.rollbacks = []

	def run(self, state):
		self.runs.append(state.staged)
		return self.outcome

	def rollback_to(self, sha):
		self.rollbacks.append(sha)
		return self.rollback_ok
```

- [ ] **Step 2: Write the failing tests `tests/test_app.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

import datetime
import json
import tempfile
import unittest
from pathlib import Path

from mmo_deployer.app import Deployer, write_request
from mmo_deployer.maintenance import Outcome
from mmo_deployer.stage import StageError
from mmo_deployer.state import State, load_state, save_state

from .fakes import FakeClock, FakeGitHub, FakeMaintenance, FakeNotifier, FakePatchDir, FakeStager, make_config

UTC = datetime.timezone.utc
NEW = "n" * 40
OLD = "o" * 40


def at(hour, minute, day=7):
	return datetime.datetime(2026, 10, day, hour, minute, tzinfo=UTC)


class AppTests(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.cfg = make_config(self.tmp.name)
		self.cfg.state_dir.mkdir(parents=True)
		self.clock = FakeClock(at(1, 0))
		self.github = FakeGitHub()
		self.github.add_release("nightly-20261007-nnnnnnnn", NEW, {}, "2026-10-07T00:50:00Z")
		self.stager = FakeStager()
		self.maintenance = FakeMaintenance()
		self.patchdir = FakePatchDir(self.tmp.name, {OLD, NEW}, current=OLD)
		self.notifier = FakeNotifier()
		self.state_path = self.cfg.state_dir / "state.json"
		save_state(self.state_path, State(live=OLD))
		self.app = Deployer(self.cfg, self.github, self.stager, self.maintenance, self.patchdir, self.notifier, self.clock)

	def tearDown(self):
		self.tmp.cleanup()

	def state(self):
		return load_state(self.state_path)

	def test_tick_stages_newest_release(self):
		self.app.tick()
		state = self.state()
		self.assertEqual(state.staged, NEW)
		self.assertEqual(state.staged_tag, "nightly-20261007-nnnnnnnn")
		self.assertEqual(state.staged_changes, ["fix(loot): roll on the right table"])
		self.assertTrue(any("Staged" in m for m in self.notifier.messages))
		self.assertEqual(self.maintenance.runs, [])

	def test_poll_interval_is_respected(self):
		self.app.tick()
		self.stager.staged.clear()
		save_state(self.state_path, State(live=OLD))
		self.clock.sleep(60)
		self.app.tick()
		self.assertEqual(self.stager.staged, [])

	def test_stage_failure_notifies_once(self):
		self.stager.error = StageError("git fetch failed")
		self.app.tick()
		self.clock.sleep(self.cfg.poll_interval_s)
		self.app.tick()
		self.assertEqual(self.stager.staged, [NEW, NEW])
		self.assertEqual(sum("Staging" in m for m in self.notifier.messages), 1)
		self.assertIsNone(self.state().staged)
		self.assertEqual(self.state().live, OLD)

	def test_listing_error_is_survivable(self):
		self.github.list_error = OSError("dns")
		self.app.tick()
		self.assertIsNone(self.state().staged)

	def test_maintenance_runs_in_window_and_promotes(self):
		news = Path(self.tmp.name) / "launcher" / "launcher.json"
		news.parent.mkdir()
		news.write_text(json.dumps({"version": 1, "patches": []}), encoding="utf-8")
		self.app.tick()
		self.clock.current = at(4, 46)
		self.app.tick()
		state = self.state()
		self.assertEqual(self.maintenance.runs, [NEW])
		self.assertEqual(state.live, NEW)
		self.assertEqual(state.previous_live, OLD)
		self.assertIsNone(state.staged)
		self.assertEqual(state.last_maintenance_date, "2026-10-07")
		self.assertEqual(self.patchdir.pruned, [(3, {NEW, OLD})])
		self.assertEqual(json.loads(news.read_text(encoding="utf-8"))["patches"][0]["title"], "Update 2026-10-07")
		self.assertTrue(any("is live" in m for m in self.notifier.messages))

	def test_maintenance_runs_once_per_day(self):
		self.app.tick()
		self.clock.current = at(4, 46)
		self.maintenance.outcome = Outcome.ABORTED
		self.app.tick()
		self.clock.current = at(4, 50)
		self.app.tick()
		self.assertEqual(self.maintenance.runs, [NEW])

	def test_restart_in_the_afternoon_does_not_trigger_maintenance(self):
		save_state(self.state_path, State(live=OLD, staged=NEW))
		self.clock.current = at(14, 0)
		self.app.tick()
		self.assertEqual(self.maintenance.runs, [])
		self.clock.current = at(4, 50, day=8)
		self.app.tick()
		self.assertEqual(self.maintenance.runs, [NEW])

	def test_nothing_staged_means_no_maintenance(self):
		self.github.releases.clear()
		self.clock.current = at(4, 46)
		self.app.tick()
		self.assertEqual(self.maintenance.runs, [])

	def test_rolled_back_release_is_marked_bad(self):
		self.maintenance.outcome = Outcome.ROLLED_BACK
		save_state(self.state_path, State(live=OLD, staged=NEW))
		self.clock.current = at(4, 46)
		self.app.tick()
		state = self.state()
		self.assertEqual(state.bad, [NEW])
		self.assertIsNone(state.staged)
		self.assertEqual(state.live, OLD)
		self.assertFalse(state.paused)

	def test_failed_release_pauses(self):
		self.maintenance.outcome = Outcome.FAILED
		save_state(self.state_path, State(live=OLD, staged=NEW))
		self.clock.current = at(4, 46)
		self.app.tick()
		self.assertTrue(self.state().paused)
		self.assertTrue(any("MANUAL INTERVENTION" in m for m in self.notifier.messages))

	def test_paused_skips_staging_and_maintenance(self):
		save_state(self.state_path, State(live=OLD, staged=NEW, paused=True))
		self.clock.current = at(4, 46)
		self.app.tick()
		self.assertEqual(self.stager.staged, [])
		self.assertEqual(self.maintenance.runs, [])

	def test_pause_request_is_applied_and_persisted(self):
		write_request(self.cfg.state_dir, "pause", [])
		self.app.tick()
		self.assertTrue(self.state().paused)
		self.assertEqual(list((self.cfg.state_dir / "requests").glob("*.json")), [])
		write_request(self.cfg.state_dir, "resume", [])
		self.app.tick()
		self.assertFalse(self.state().paused)

	def test_deploy_now_outside_window(self):
		write_request(self.cfg.state_dir, "deploy-now", [NEW])
		self.clock.current = at(13, 0)
		self.app.tick()
		self.assertEqual(self.maintenance.runs, [NEW])
		self.assertEqual(self.state().live, NEW)

	def test_deploy_now_unknown_sha(self):
		write_request(self.cfg.state_dir, "deploy-now", ["x" * 40])
		self.app.next_poll = self.clock.now() + datetime.timedelta(hours=1)
		self.app.tick()
		self.assertEqual(self.maintenance.runs, [])
		self.assertTrue(any("no nightly- release" in m for m in self.notifier.messages))

	def test_manual_rollback(self):
		save_state(self.state_path, State(live=NEW, previous_live=OLD))
		write_request(self.cfg.state_dir, "rollback", [])
		self.app.tick()
		state = self.state()
		self.assertEqual(self.maintenance.rollbacks, [OLD])
		self.assertEqual(state.live, OLD)
		self.assertIsNone(state.previous_live)
		self.assertEqual(state.bad, [NEW])

	def test_manual_rollback_without_previous(self):
		write_request(self.cfg.state_dir, "rollback", [])
		self.app.tick()
		self.assertEqual(self.maintenance.rollbacks, [])

	def test_unbad(self):
		save_state(self.state_path, State(live=OLD, bad=[NEW]))
		write_request(self.cfg.state_dir, "unbad", [NEW])
		self.app.next_poll = self.clock.now() + datetime.timedelta(hours=1)
		self.app.tick()
		self.assertEqual(self.state().bad, [])

	def test_malformed_request_is_dropped(self):
		requests = self.cfg.state_dir / "requests"
		requests.mkdir()
		(requests / "1-broken.json").write_text("{nope", encoding="utf-8")
		self.app.tick()
		self.assertEqual(list(requests.glob("*.json")), [])


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `python -m unittest discover -s deploy/deployer/tests -t deploy/deployer -p "test_app.py"`
Expected: FAIL with `ModuleNotFoundError: No module named 'mmo_deployer.app'`.

- [ ] **Step 4: Implement `mmo_deployer/app.py`**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""The deployer loop: stage new releases, run the nightly maintenance, apply CLI requests.

Only this loop writes state.json. CLI commands drop request files that the next tick
applies, so a command can never be overwritten by a tick that was already running.
"""

import datetime
import json
import logging
import os
import time
from pathlib import Path

from .backup import prune_backups
from .maintenance import Outcome
from .news import add_patch_note
from .stage import StageError, select_release
from .state import load_state, record, save_state
from .web import HttpError

log = logging.getLogger("deployer")

TICK_S = 10
MAINTENANCE_WINDOW_S = 3600


def _short(sha):
	return sha[:8] if sha else "none"


def write_request(state_dir, command, args):
	requests = Path(state_dir) / "requests"
	requests.mkdir(parents=True, exist_ok=True)
	name = "{}-{}".format(time.time_ns(), command)
	tmp = requests / (name + ".tmp")
	tmp.write_text(json.dumps({"command": command, "args": list(args)}), encoding="utf-8")
	os.replace(tmp, requests / (name + ".json"))


class Deployer:
	def __init__(self, cfg, github, stager, maintenance, patchdir, notifier, clock):
		self.cfg = cfg
		self.github = github
		self.stager = stager
		self.maintenance = maintenance
		self.patchdir = patchdir
		self.notifier = notifier
		self.clock = clock
		self.state_path = Path(cfg.state_dir) / "state.json"
		self.requests_dir = Path(cfg.state_dir) / "requests"
		self.next_poll = None
		self.stage_failures = set()

	def run_forever(self):
		state = load_state(self.state_path)
		self.notifier.send("Deployer started (live {}, staged {}, dry run {}).".format(
			_short(state.live), _short(state.staged), self.cfg.dry_run))
		while True:
			try:
				self.tick()
			except Exception:
				# Keep the service alive; the next tick starts from state.json again.
				log.exception("tick failed")
			self.clock.sleep(TICK_S)

	def tick(self):
		state = load_state(self.state_path)
		self._handle_requests(state)
		now = self.clock.now()
		if not state.paused and (self.next_poll is None or now >= self.next_poll):
			self.next_poll = now + datetime.timedelta(seconds=self.cfg.poll_interval_s)
			self.stage_newest(state)
			save_state(self.state_path, state)
		now = self.clock.now()
		if self.maintenance_due(state, now):
			state.last_maintenance_date = now.date().isoformat()
			# Saved first, so a crash mid-maintenance cannot repeat it the same night.
			save_state(self.state_path, state)
			self.run_maintenance(state)
		save_state(self.state_path, state)

	def maintenance_due(self, state, now):
		if state.paused or not state.staged or state.staged == state.live:
			return False
		if state.last_maintenance_date == now.date().isoformat():
			return False
		start = now.replace(hour=self.cfg.maintenance_at.hour, minute=self.cfg.maintenance_at.minute, second=0, microsecond=0)
		return start <= now < start + datetime.timedelta(seconds=MAINTENANCE_WINDOW_S)

	def stage_newest(self, state):
		try:
			releases = self.github.list_releases(self.cfg.release_prefix)
		except (HttpError, OSError, ValueError, KeyError) as error:
			log.warning("could not list releases: %s", error)
			return
		release = select_release(releases, state)
		if release is not None:
			self._stage(state, release)

	def _stage(self, state, release):
		commit = release["target_commitish"]
		try:
			manifest = self.stager.stage(release)
		except StageError as error:
			log.error("staging %s failed: %s", release["tag_name"], error)
			if commit not in self.stage_failures:
				self.stage_failures.add(commit)
				self.notifier.send("Staging {} failed; retrying every poll, live release unchanged: {}".format(release["tag_name"], error))
			return False
		self.stage_failures.discard(commit)
		state.staged = commit
		state.staged_tag = release["tag_name"]
		state.staged_changes = list(manifest.get("changes") or [])
		record(state, self.clock.now(), "staged", commit=commit, tag=release["tag_name"])
		lines = "\n".join("- " + change for change in state.staged_changes[:20])
		self.notifier.send("Staged {} ({} changes) for the {} maintenance.\n{}".format(
			release["tag_name"], len(state.staged_changes), self.cfg.maintenance_at.strftime("%H:%M"), lines))
		return True

	def run_maintenance(self, state):
		sha = state.staged
		outcome = self.maintenance.run(state)
		now = self.clock.now()
		record(state, now, "maintenance", outcome=outcome, commit=sha)
		if outcome == Outcome.DEPLOYED:
			self._promote(state, now)
		elif outcome in (Outcome.ROLLED_BACK, Outcome.FAILED):
			state.bad.append(sha)
			state.staged = None
			state.staged_tag = None
			state.staged_changes = []
			if outcome == Outcome.ROLLED_BACK:
				self.notifier.send("Release {} was rolled back; {} is live again. It will not be staged again (deployer unbad {} to retry).".format(
					_short(sha), _short(state.live), sha))
			else:
				state.paused = True
				self.notifier.send("MANUAL INTERVENTION REQUIRED: release {} failed and the rollback did not recover. The deployer is paused.".format(_short(sha)))
		return outcome

	def _promote(self, state, now):
		state.previous_live = state.live
		state.live = state.staged
		changes = state.staged_changes
		state.staged = None
		state.staged_tag = None
		state.staged_changes = []
		self.patchdir.prune(self.cfg.keep_releases, {sha for sha in (state.live, state.previous_live) if sha})
		prune_backups(Path(self.cfg.state_dir) / "backups", self.cfg.keep_backups)
		news = self.patchdir.root / "launcher" / "launcher.json"
		if not add_patch_note(news, now.strftime("%Y-%m-%d"), changes):
			log.warning("no launcher.json at %s; patch notes not published", news)
		lines = "\n".join("- " + change for change in changes[:20])
		self.notifier.send("Release {} is live.\n{}".format(_short(state.live), lines))

	def _handle_requests(self, state):
		if not self.requests_dir.is_dir():
			return
		for path in sorted(self.requests_dir.glob("*.json")):
			try:
				request = json.loads(path.read_text(encoding="utf-8"))
				self._apply_request(state, request["command"], list(request.get("args") or []))
			except (ValueError, KeyError, TypeError) as error:
				log.error("dropping malformed request %s: %s", path.name, error)
			path.unlink()
			save_state(self.state_path, state)

	def _apply_request(self, state, command, args):
		record(state, self.clock.now(), "request", command=command, args=args)
		if command == "pause":
			state.paused = True
		elif command == "resume":
			state.paused = False
		elif command == "unbad":
			state.bad = [sha for sha in state.bad if sha != args[0]]
		elif command == "stage-now":
			self.next_poll = None
		elif command == "deploy-now":
			self._deploy_now(state, args[0] if args else None)
		elif command == "rollback":
			self._manual_rollback(state)
		else:
			log.error("unknown request %s", command)

	def _deploy_now(self, state, sha):
		if sha and sha != state.staged:
			try:
				releases = self.github.list_releases(self.cfg.release_prefix)
			except (HttpError, OSError, ValueError, KeyError) as error:
				self.notifier.send("deploy-now: could not list releases: {}".format(error))
				return
			matches = [r for r in releases if r["target_commitish"] == sha]
			if not matches:
				self.notifier.send("deploy-now: no {} release for {}".format(self.cfg.release_prefix, sha))
				return
			if not self._stage(state, matches[0]):
				return
		if not state.staged or state.staged == state.live:
			self.notifier.send("deploy-now: nothing new is staged")
			return
		self.run_maintenance(state)

	def _manual_rollback(self, state):
		target = state.previous_live
		if not target:
			self.notifier.send("rollback: there is no previous release to return to")
			return
		self.notifier.send("Manual rollback from {} to {}: servers restart now.".format(_short(state.live), _short(target)))
		if self.maintenance.rollback_to(target):
			state.bad.append(state.live)
			state.live = target
			state.previous_live = None
			self.notifier.send("Rollback done: {} is live.".format(_short(target)))
		else:
			state.paused = True
			self.notifier.send("MANUAL INTERVENTION REQUIRED: rollback to {} failed. The deployer is paused.".format(_short(target)))
```

Note on `test_deploy_now_unknown_sha` and `test_unbad`: they set `app.next_poll` into the future so the regular poll does not stage `NEW` in the same tick.

- [ ] **Step 5: Implement `mmo_deployer/cli.py`, `__main__.py` and `deployer.sh`**

`mmo_deployer/cli.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""`deployer <command>`: `run` is the service; every other command talks to it via request files."""

import argparse
import json
import logging
import os
import sys
from dataclasses import asdict
from pathlib import Path

from .app import TICK_S, Deployer, write_request
from .backup import dump_databases
from .clients import GitHubClient, Notifier, PortainerClient, RestApi
from .clock import Clock
from .config import ConfigError, load_config
from .maintenance import Maintenance
from .patchdir import PatchDir
from .stage import Stager
from .state import load_state
from .web import Http

REQUEST_COMMANDS = ("pause", "resume", "stage-now", "deploy-now", "rollback", "unbad")


def build_deployer(cfg, clock=None, backup=None, compiler_command=None, github_api="https://api.github.com"):
	clock = clock or Clock(cfg.timezone)
	http = Http(timeout=60)
	notifier = Notifier(cfg.notify_webhook, cfg.notify_format, http)
	github = GitHubClient(cfg.github_repo, cfg.github_token, http, github_api)
	patchdir = PatchDir(cfg.patch_root)
	stager = Stager(cfg, github, patchdir, compiler_command)
	realms = [RestApi(realm.name, realm.url, realm.password, http) for realm in cfg.realms]
	login = RestApi("login", cfg.login_url, cfg.login_password, http)
	# Portainer answers the stack update only after pulling and starting every image.
	portainer = PortainerClient(cfg.portainer_url, cfg.portainer_token, cfg.portainer_stack_id, cfg.portainer_endpoint_id, Http(timeout=900))
	if backup is None:
		backups_dir = Path(cfg.state_dir) / "backups"

		def backup(sha):
			label = "{}-{}".format(clock.now().strftime("%Y%m%d-%H%M%S"), sha[:8])
			dump_databases(cfg, backups_dir / label)

	maintenance = Maintenance(cfg, realms, login, portainer, patchdir, backup, notifier, clock)
	return Deployer(cfg, github, stager, maintenance, patchdir, notifier, clock)


def main(argv=None, env=None):
	parser = argparse.ArgumentParser(prog="deployer", description=__doc__)
	sub = parser.add_subparsers(dest="command")
	sub.add_parser("run", help="run the service (default)")
	sub.add_parser("status", help="print state.json")
	sub.add_parser("pause", help="skip staging and maintenance until resumed")
	sub.add_parser("resume")
	sub.add_parser("stage-now", help="poll GitHub on the next tick")
	deploy_now = sub.add_parser("deploy-now", help="run the maintenance now (with countdown)")
	deploy_now.add_argument("sha", nargs="?")
	sub.add_parser("rollback", help="return to the previous live release immediately")
	unbad = sub.add_parser("unbad", help="allow a rolled-back release to be staged again")
	unbad.add_argument("sha")
	args = parser.parse_args(argv)
	command = args.command or "run"

	logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
	try:
		cfg = load_config(os.environ if env is None else env)
	except ConfigError as error:
		print("configuration error: {}".format(error), file=sys.stderr)
		return 2

	if command == "run":
		build_deployer(cfg).run_forever()
		return 0
	if command == "status":
		state = asdict(load_state(Path(cfg.state_dir) / "state.json"))
		state["history"] = state["history"][-10:]
		print(json.dumps(state, indent=2))
		return 0
	request_args = [getattr(args, "sha")] if getattr(args, "sha", None) else []
	write_request(cfg.state_dir, command, request_args)
	print("queued '{}'; the deployer applies it within {} s (see 'deployer status').".format(command, TICK_S))
	return 0
```

`mmo_deployer/__main__.py`:
```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
import sys

from .cli import main

sys.exit(main())
```

`deploy/deployer/deployer.sh`:
```sh
#!/bin/sh
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
# `docker exec mmo-deployer deployer status` etc.
cd /app && exec python3 -m mmo_deployer "$@"
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `python -m unittest discover -s deploy/deployer/tests -t deploy/deployer -p "test_*.py"`
Expected: `OK`.

- [ ] **Step 7: Check that the CLI parses**

Run (from `deploy/deployer`): `python -m mmo_deployer --help`
Expected: usage listing the commands `run`, `status`, `pause`, `resume`, `stage-now`, `deploy-now`, `rollback` and `unbad`. Then `python -m mmo_deployer status` with no environment set should exit 2 with "configuration error: missing environment variables: …".

- [ ] **Step 8: Commit**

```bash
git add deploy/deployer
git commit -m "feat(deployer): service loop, request queue and CLI"
```

---

### Task 11: End-to-end integration test (stubbed remote systems)

**Files:**
- Test: `deploy/deployer/tests/test_integration.py`

**Interfaces:**
- Consumes: `cli.build_deployer` (Task 10); `StubServer` (Task 6); `make_git_repo`, `make_release_assets`, `CAN_SYMLINK`, `FakeClock`, `make_config` (fakes).

- [ ] **Step 1: Write the test**

```python
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""A whole night with real clients, real git, a real PatchDir and HTTP stubs for every remote."""

import dataclasses
import datetime
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

from mmo_deployer.cli import build_deployer
from mmo_deployer.config import RealmEndpoint
from mmo_deployer.state import State, load_state, save_state

from .fakes import CAN_SYMLINK, FakeClock, make_config, make_git_repo, make_release_assets
from .stub_server import StubServer

FAKE_COMPILER = [sys.executable, os.path.join(os.path.dirname(__file__), "fake_update_compiler.py")]
NEW = "a" * 40
OLD = "b" * 40


@unittest.skipUnless(CAN_SYMLINK, "needs symlink support (runs in the Linux CI gate)")
class FullNight(unittest.TestCase):
	def test_stage_then_deploy(self):
		with tempfile.TemporaryDirectory() as tmp, StubServer() as stub:
			root = Path(tmp)
			data_commit = make_git_repo(root / "mmo-data", {"Worlds/map.txt": "terrain"})
			assets = make_release_assets(NEW, data_commit, changes=["fix(loot): roll on the right table"])

			stub.route("GET", "/repos/Kyoril/mmo/releases", (200, [{
				"tag_name": "nightly-20261007-aaaaaaaa", "target_commitish": NEW, "created_at": "2026-10-07T00:50:00Z",
				"draft": False, "assets": [{"name": "client-bin.zip", "id": 1}, {"name": "release.json", "id": 2}]}]))
			stub.route("GET", "/repos/Kyoril/mmo/releases/assets/1", (200, assets["client-bin.zip"]))
			stub.route("GET", "/repos/Kyoril/mmo/releases/assets/2", (200, assets["release.json"]))
			for prefix in ("/realm", "/login"):
				stub.route("GET", prefix + "/uptime", (200, {"uptime": 5}))
			stub.route("POST", "/realm/shutdown", (200, {"status": "SUCCESS", "delay": 900}))
			stub.route("GET", "/api/status", (200, {"Version": "2.21"}))
			stub.route("GET", "/api/stacks/7", (200, {"Id": 7, "Name": "mmo", "Env": [{"name": "MMO_TAG", "value": OLD}]}))
			stub.route("GET", "/api/stacks/7/file", (200, {"StackFileContent": "services: {}\n"}))
			stub.route("PUT", "/api/stacks/7", (200, {"Id": 7}))
			stub.route("GET", "/api/endpoints/2/docker/containers/json", (200, []))

			cfg = dataclasses.replace(
				make_config(tmp, data_repo_url=str(root / "mmo-data")),
				portainer_url=stub.url,
				realms=(RealmEndpoint("realm01", stub.url + "/realm", "pw"),),
				login_url=stub.url + "/login",
			)
			cfg.state_dir.mkdir(parents=True)
			(cfg.patch_root / "releases" / OLD).mkdir(parents=True)
			os.symlink(os.path.join("releases", OLD), cfg.patch_root / "current")
			(cfg.patch_root / "launcher").mkdir()
			(cfg.patch_root / "launcher" / "launcher.json").write_text(json.dumps({"version": 1, "patches": []}), encoding="utf-8")
			save_state(cfg.state_dir / "state.json", State(live=OLD))

			clock = FakeClock(datetime.datetime(2026, 10, 7, 1, 0, tzinfo=datetime.timezone.utc))
			backups = []
			deployer = build_deployer(cfg, clock=clock, backup=backups.append, compiler_command=FAKE_COMPILER, github_api=stub.url)

			deployer.tick()
			self.assertEqual(load_state(cfg.state_dir / "state.json").staged, NEW)
			self.assertTrue((cfg.patch_root / "releases" / NEW / "list.txt").is_file())
			self.assertEqual(os.readlink(cfg.patch_root / "current"), os.path.join("releases", OLD))

			clock.current = datetime.datetime(2026, 10, 7, 4, 45, tzinfo=datetime.timezone.utc)
			deployer.tick()

			state = load_state(cfg.state_dir / "state.json")
			self.assertEqual((state.live, state.previous_live, state.staged), (NEW, OLD, None))
			self.assertEqual(os.readlink(cfg.patch_root / "current"), os.path.join("releases", NEW))
			self.assertEqual(backups, [NEW])
			self.assertEqual(stub.find("POST", "/realm/shutdown")[0].form(), {"delay": "900"})
			put = stub.find("PUT", "/api/stacks/7")[0].json()
			self.assertIn({"name": "MMO_TAG", "value": NEW}, put["env"])
			news = json.loads((cfg.patch_root / "launcher" / "launcher.json").read_text(encoding="utf-8"))
			self.assertIn("Roll on the right table", news["patches"][0]["body"])


if __name__ == "__main__":
	unittest.main()
```

- [ ] **Step 2: Run it**

Run: `python -m unittest discover -s deploy/deployer/tests -t deploy/deployer -p "test_integration.py" -v`
Expected: `ok`, or `skipped 'needs symlink support…'` on Windows without developer mode. If it is skipped locally, also run it under Linux:

```bash
docker run --rm -v "C:/Kyoril/mmo/deploy/deployer:/d" -w /d python:3.12-slim sh -c "apt-get update -qq && apt-get install -qq -y git >/dev/null && python -m unittest discover -s tests -t . -p 'test_*.py' -v"
```
Expected: every deployer test passes, including `test_stage_then_deploy`.

- [ ] **Step 3: Commit**

```bash
git add deploy/deployer/tests/test_integration.py
git commit -m "test(deployer): full stage-and-deploy night against stubbed remotes"
```

---

### Task 12: Deployer image with a real `update_compiler`

**Files:**
- Create: `Dockerfile.deployer`, `Dockerfile.deployer.dockerignore`, `deploy/deployer/smoke_update_compiler.sh`
- Modify: `.github/workflows/docker-build.yml` (matrix entry)

**Interfaces:**
- Produces: image `<DOCKER_USERNAME>/mmo_deployer:<tag>`. Inside it: `/app/update_compiler` (Linux build, same commit), `/app/mmo_deployer`, `/usr/local/bin/deployer`, `/app/smoke_update_compiler.sh`. Default CMD `deployer run`.

- [ ] **Step 1: Create `Dockerfile.deployer.dockerignore`** (BuildKit uses it for this Dockerfile only)

```
.git/
build/
bin/
data/client/
data/editor/
e2e/runtime/
```

- [ ] **Step 2: Create `Dockerfile.deployer`**

```dockerfile
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y gcc-13 g++-13 cmake git zlib1g-dev libzstd-dev libssl-dev libmariadb-dev-compat uuid-dev

COPY . /app
WORKDIR /app

ENV CC=gcc-13
ENV CXX=g++-13
RUN mkdir -p docker-build && \
	cd docker-build && \
	cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DMMO_OPENSSL_STATIC=OFF -DMMO_BUILD_TOOLS=ON -DMMO_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release /app && \
	make -j4 update_compiler


FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
		python3 git ca-certificates mariadb-client tzdata libssl3 zlib1g libzstd1 && \
	rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/bin/update_compiler /app/update_compiler
COPY deploy/deployer/mmo_deployer /app/mmo_deployer
COPY deploy/deployer/deployer.sh /usr/local/bin/deployer
COPY deploy/deployer/smoke_update_compiler.sh /app/smoke_update_compiler.sh
RUN chmod +x /app/update_compiler /usr/local/bin/deployer /app/smoke_update_compiler.sh

ENV PYTHONUNBUFFERED=1
CMD ["deployer", "run"]
```

- [ ] **Step 3: Create `deploy/deployer/smoke_update_compiler.sh`**

```sh
#!/bin/sh
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
# Compiles a miniature client tree with the real deploy/patch/source.txt layout and checks
# that the Linux update_compiler produces a list.txt naming the Windows binaries.
set -eu
work=$(mktemp -d)
mkdir -p "$work/deploy/patch"
for dir in Config Fonts Interface Models Sound Textures ClientDB Worlds \
	Locales/Locale_deDE Locales/Locale_enUS Locales/Locale_frFR Locales/Locale_ruRU; do
	mkdir -p "$work/data/client/$dir"
	echo "smoke" > "$work/data/client/$dir/file.txt"
done
for bin in Launcher.exe fmod.dll mmo_client.exe mmo_error.exe; do
	echo "$bin" > "$work/deploy/patch/$bin"
done
cat > "$work/deploy/patch/source.txt" <<'EOF'
version = 0
root = (type = "fs", from = ".", to = "", entries =
{
	(type = "if", condition = "WINDOWS", value =
		(type = "fs", from = "Launcher.exe")
	)
	(type = "fs", from = "../../data/client", to = "Data", entries =
	{
		(type = "hpak2", from = "Config", to = "Misc.hpak", sub = "Config")
		(type = "hpak2", from = "Fonts", to = "Fonts.hpak", sub = "Fonts")
		(type = "hpak2", from = "Interface", to = "Interface.hpak", sub = "Interface")
		(type = "hpak2", from = "Models", to = "Models.hpak", sub = "Models")
		(type = "hpak2", from = "Sound", to = "Sound.hpak", sub = "Sound")
		(type = "hpak2", from = "Textures", to = "Textures.hpak", sub = "Textures")
		(type = "hpak2", from = "ClientDB", to = "ClientDB.hpak", sub = "ClientDB")
		(type = "hpak2", from = "Worlds", to = "Worlds.hpak", sub = "Worlds")
		(type = "fs", from = "Locales", to = "Locales", entries =
		{
			(type = "hpak2", from = "Locale_deDE", to = "Locale_deDE.hpak")
			(type = "hpak2", from = "Locale_enUS", to = "Locale_enUS.hpak")
			(type = "hpak2", from = "Locale_frFR", to = "Locale_frFR.hpak")
			(type = "hpak2", from = "Locale_ruRU", to = "Locale_ruRU.hpak")
		})
	})
	(type = "if", condition = "WINDOWS", value =
		(type = "fs", from = "fmod.dll")
	)
	(type = "if", condition = "WINDOWS", value =
		(type = "fs", from = "mmo_client.exe")
	)
	(type = "if", condition = "WINDOWS", value =
		(type = "fs", from = "mmo_error.exe")
	)
})
EOF
/app/update_compiler -s "$work/deploy/patch" -o "$work/out" -c zlib -j 2
for bin in Launcher.exe mmo_client.exe mmo_error.exe fmod.dll; do
	grep -q "$bin" "$work/out/list.txt" || { echo "list.txt lacks $bin"; exit 1; }
done
echo "update_compiler smoke test passed"
```

The heredoc is the verbatim content of `deploy/patch/source.txt` (Task 2), so the smoke test compiles the real layout. Add this test to `tools/tests/test_release_tools.py` to keep the two copies from drifting:

```python
class SmokeTestUsesRealSource(unittest.TestCase):
	def test_smoke_script_embeds_source_txt(self):
		with open(os.path.join(REPO_ROOT, "deploy", "patch", "source.txt"), encoding="utf-8") as handle:
			source = handle.read().strip()
		with open(os.path.join(REPO_ROOT, "deploy", "deployer", "smoke_update_compiler.sh"), encoding="utf-8") as handle:
			script = handle.read()
		self.assertIn(source, script)
```

- [ ] **Step 4: Build the image and run the smoke test locally**

```bash
docker build -f Dockerfile.deployer -t mmo_deployer:dev .
docker run --rm mmo_deployer:dev /app/smoke_update_compiler.sh
docker run --rm mmo_deployer:dev deployer --help
```
Expected:
- the build succeeds
- the smoke test prints `update_compiler smoke test passed`
- `--help` lists the commands

If the CMake configure fails because `MMO_BUILD_TOOLS=ON` pulls in a tool that does not build on Linux, find the failing tool in the build log. Wrap only that tool's `add_subdirectory` in `src/CMakeLists.txt` in `if (WIN32)`, as already done for `mmo_error`. Never touch `update_compiler`'s own guard.

- [ ] **Step 5: Add the image to CI**

In `.github/workflows/docker-build.yml`, append to the matrix:

```yaml
          - image: mmo_deployer
            dockerfile: ./Dockerfile.deployer
```

Append a step after "Build and push" that runs only for the deployer image:

```yaml
      - name: Smoke-test update_compiler in the deployer image
        if: matrix.image == 'mmo_deployer'
        shell: bash
        run: |
          first_tag=$(echo "${{ steps.tags.outputs.list }}" | head -n1)
          docker run --rm "$first_tag" /app/smoke_update_compiler.sh
```

- [ ] **Step 6: Run the tool tests and commit**

Run: `python tools/tests/test_release_tools.py`
Expected: `OK`.

```bash
git add Dockerfile.deployer Dockerfile.deployer.dockerignore deploy/deployer/smoke_update_compiler.sh .github/workflows/docker-build.yml tools/tests/test_release_tools.py
git commit -m "build(deployer): image with a Linux update_compiler and smoke test"
```

---

### Task 13: Compose files for the game stack and the deployer stack

**Files:**
- Modify: `compose.yml`
- Create: `deploy/deployer/compose.yml`

**Interfaces:**
- Produces:
  - The game stack's images follow `${MMO_TAG}`, which the deployer sets through Portainer.
  - The network is named `mmo_game_servers` regardless of the stack name, so the deployer stack can join it as external.
  - Realm and world start only after `data-service` has refreshed the shared volume.

- [ ] **Step 1: Edit `compose.yml`**

Images: replace `:latest` with `:${MMO_TAG:-latest}` on all four (`mmo_data`, `mmo_login_server`, `mmo_realm_server`, `mmo_world_server`).

`data-service` command: sync with delete semantics and fail loudly instead of `; true`:

```yaml
    command: ["/bin/sh", "-c", "set -e; rm -rf /server_data/data /server_data/nav /server_data/client; mkdir -p /server_data/data /server_data/nav /server_data/client; cp -r /data/. /server_data/data/; cp -r /nav/. /server_data/nav/; cp -r /client/Worlds /server_data/client/"]
```

`realm_server_01` and `world_node_01`: add

```yaml
    depends_on:
      data-service:
        condition: service_completed_successfully
```

Networks: give the network a stable name.

```yaml
networks:
  mmo_game_servers:
    name: mmo_game_servers
    driver: bridge
```

Add one comment line above `volumes:`:
```yaml
# MMO_TAG (a full commit SHA) is set by the deployer through Portainer; see docs/deployment.md.
```

- [ ] **Step 2: Create `deploy/deployer/compose.yml`**

```yaml
# Portainer stack for mmo-deployer. Pin DEPLOYER_TAG to a commit SHA; see docs/deployment.md.
services:
  deployer:
    image: kyoril/mmo_deployer:${DEPLOYER_TAG}
    container_name: mmo-deployer
    restart: unless-stopped
    environment:
      - GITHUB_REPO=Kyoril/mmo
      - GITHUB_TOKEN=${GITHUB_TOKEN}
      - DATA_REPO_URL=https://github.com/Kyoril/mmo-data
      - PORTAINER_URL=${PORTAINER_URL}
      - PORTAINER_TOKEN=${PORTAINER_TOKEN}
      - PORTAINER_STACK_ID=${PORTAINER_STACK_ID}
      - PORTAINER_ENDPOINT_ID=${PORTAINER_ENDPOINT_ID}
      - WAIT_SERVICES=realm_server_01,world_node_01
      - REALMS=${REALMS}
      - LOGIN_URL=http://mmo-dev-login:8090
      - LOGIN_PASSWORD=${LOGIN_PASSWORD}
      - MYSQL_HOST=host.docker.internal
      - MYSQL_USER=${MYSQL_USER}
      - MYSQL_PASSWORD=${MYSQL_PASSWORD}
      - BACKUP_DATABASES=mmo_login,mmo_realm_01
      - MAINTENANCE_AT=04:45
      - TZ=Europe/Berlin
      - COUNTDOWN_S=900
      - NOTIFY_WEBHOOK=${NOTIFY_WEBHOOK}
      - NOTIFY_FORMAT=discord
      - DRY_RUN=${DRY_RUN:-1}
    volumes:
      - /srv/mmo-patch:/srv/mmo-patch
      - deployer-state:/state
    extra_hosts:
      host.docker.internal: "172.17.0.1"
    networks:
      - mmo_game_servers

volumes:
  deployer-state:

networks:
  mmo_game_servers:
    external: true
```

`DRY_RUN` defaults to `1`, so a freshly deployed deployer never touches the live stack until it is switched off on purpose.

- [ ] **Step 3: Validate both files**

```bash
docker compose -f compose.yml config --quiet
MMO_TAG=abc docker compose -f compose.yml config | grep "image:"
DEPLOYER_TAG=abc docker compose -f deploy/deployer/compose.yml config --quiet
```
Expected: no errors, and every game image ends in `:abc`. The deployer file may warn about unset variables, which is fine.

- [ ] **Step 4: Commit**

```bash
git add compose.yml deploy/deployer/compose.yml
git commit -m "deploy: tag-driven game stack and the deployer's Portainer stack"
```

---

## Phase 4 — Docs and finish

### Task 14: Runbook and project rules

**Files:**
- Create: `docs/deployment.md`
- Modify: `CLAUDE.md` (sections "Database Migrations" and "Agentic Workflow")

- [ ] **Step 1: Write `docs/deployment.md`**

It must contain these sections, with the exact commands:

1. **Overview**
   - The nightly flow in five lines: CI at 00:30, staging, 04:45 maintenance, verify, notify.
   - Link to the spec.
2. **One-time server setup**
   1. **Patch root:**
      ```bash
      sudo mkdir -p /srv/mmo-patch/releases /srv/mmo-patch/launcher
      sudo mv <old docroot> /srv/mmo-patch/releases/legacy
      sudo ln -s releases/legacy /srv/mmo-patch/current
      sudo mv /srv/mmo-patch/releases/legacy/launcher/* /srv/mmo-patch/launcher/
      ```
   2. **nginx:** in the patch vhost, `root /srv/mmo-patch/current;` and `location /launcher/ { alias /srv/mmo-patch/launcher/; }`. Then run `sudo nginx -t && sudo systemctl reload nginx` and check that `curl -fsS https://patch.mmo-dev.net/list.txt | head -3` and `curl -fsSI https://patch.mmo-dev.net/launcher/launcher.json` both still answer.
   3. **Game stack:**
      - Paste the new `compose.yml` into the Portainer web editor.
      - Add stack env `MMO_TAG=<sha of the currently running images>`, or `latest` for the very first time.
      - Note the stack ID and endpoint ID from the Portainer URL (`#!/<endpoint>/docker/stacks/<name>?id=<stack>`).
      - The stack must be a web-editor stack, not a Git-backed one.
   4. **Portainer API token:** My account → Access tokens → add `mmo-deployer`.
   5. **GitHub token:** fine-grained, repository `Kyoril/mmo`, permission *Contents: read*. It is not needed if the repo is public.
   6. **MySQL backup user:**
      ```sql
      CREATE USER 'deployer'@'%' IDENTIFIED BY '<pw>';
      GRANT SELECT, SHOW VIEW, TRIGGER, LOCK TABLES, EVENT ON mmo_login.* TO 'deployer'@'%';
      ```
      Plus the same grant for `mmo_realm_01`.
   7. **First clone of mmo-data:** the first staging run clones about 830 MB and checks out 8 GB into the `deployer-state` volume. Expect the first stage to take a while.
   8. **Deployer stack:**
      - Paste `deploy/deployer/compose.yml` into a new Portainer stack and set its env.
      - `DEPLOYER_TAG=<sha>` of a green nightly.
      - `REALMS=[{"name":"realm01","url":"http://mmo-dev-realm-01:8092","password":"<WEB_API_PASSWORD>"}]`
      - Keep `DRY_RUN=1`.
3. **Going live**
   - Watch at least one dry-run night: a "Staged" message, then a "[dry-run] would deploy" message at 04:45.
   - Then the first real deploy, by hand and while watching: set `DRY_RUN=0`, redeploy the deployer stack, `docker exec mmo-deployer deployer deploy-now`.
   - From the second successful deploy on, automatic rollback has a previous release to return to.
4. **Daily operation:**
   - `docker exec mmo-deployer deployer status | pause | resume | stage-now | deploy-now [sha] | rollback | unbad <sha>`
   - What each notification means.
   - Where the backups are: `/state/backups` in the volume, restored with `gunzip -c <db>.sql.gz | mysql <db>`.
5. **Failure playbook:** one paragraph each for:
   - gate red: nothing shipped; look at the `gate-report` artifact
   - staging failed: data commit not pushed to `mmo-data`, or disk full
   - aborted: a realm was unreachable at 04:45
   - rolled back: the release is marked bad; fix it on develop, the next night ships
   - paused after a failed rollback: restore by hand with `deploy-now <previous sha>` or a Portainer redeploy, then `resume`
6. **Updating the deployer itself:** change `DEPLOYER_TAG` to a newer green SHA and redeploy its stack. It never updates itself.
7. **CI notes:**
   - The Linux gate runs on the runner's preinstalled MySQL. Note here if the Task 4 fallback (Windows runner) was used.
   - `docker.yml` builds develop images only after "Linux Servers" passes.
   - `win-release.yml` (master) is unchanged.

- [ ] **Step 2: Update `CLAUDE.md`**

In "Database Migrations", append:

```markdown
Migrations must be **additive**: the previous server build must keep working on the new
schema (add tables/columns; drop or rename only in a later release). Nightly deploys roll
back automatically by redeploying the previous images on the already-migrated database, so
a destructive migration turns a rollback into an outage.
```

In "Agentic Workflow", append:

```markdown
- **Deploy (nightly):** `.github/workflows/nightly-release.yml` gates develop on Linux and
  publishes a `nightly-*` release; `mmo-deployer` on the root server ships it at 04:45 with a
  15-minute realm countdown and rolls back on failed health checks. Anything merged to
  develop reaches players the next morning — see [docs/deployment.md](docs/deployment.md).
```

- [ ] **Step 3: Commit**

```bash
git add docs/deployment.md CLAUDE.md
git commit -m "docs: nightly deployment runbook and additive-migration rule"
```

---

### Task 15: Remove the temporary trigger, final gate, ship

**Files:**
- Modify: `.github/workflows/nightly-release.yml` (remove the `push:` trigger block and its comment)

- [ ] **Step 1: Remove the temporary trigger**

Delete these lines from `nightly-release.yml`:

```yaml
  # TEMPORARY: lets the feature branch prove the pipeline (publishes test-nightly-*).
  # Removed in Task 15.
  push:
    branches:
      - feature/nightly-deploy
```

The `check` job keeps its `github.event_name == 'push'` branches, which do no harm. Leave them, so the trigger can be re-enabled for a future pipeline change.

Run: `docker run --rm -v "C:/Kyoril/mmo:/repo" -w /repo rhysd/actionlint:latest -color .github/workflows/`
Expected: no output.

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/nightly-release.yml
git commit -m "ci: nightly release runs on schedule and dispatch only"
```

- [ ] **Step 3: Full gate**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gate/verify.ps1 -Tier full`
Expected: `Gate result: GREEN`, with `deployer_tests` and `tool_tests` passing.

- [ ] **Step 4: Ship**

Invoke the `/ship` skill to merge `feature/nightly-deploy` into `develop`. It does not push. Then ask the user whether to push `develop`. The scheduled workflow only starts running once `nightly-release.yml` is on origin's default branch. After that, the user follows `docs/deployment.md` "One-time server setup" on the root server. Those steps run on the user's infrastructure, so an agent must not do them unasked.

- [ ] **Step 5: Clean up test releases (with the user's OK)**

List the releases the feature-branch runs created: `gh release list --limit 50 | grep test-nightly-`. Ask the user before deleting them, since deleting releases cannot be undone. On yes: `gh release delete <tag> --cleanup-tag --yes` for each.
