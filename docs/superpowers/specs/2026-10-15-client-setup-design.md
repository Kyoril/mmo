# Client Setup (Installer) — Design

Date: 2026-10-15
Status: approved design, not implemented

## Problem

Players download a bare `Launcher.exe` from mmo-dev.net. It installs the whole game into
whatever folder it sits in (often `Downloads\`), never asks where the game should go, and a
browser rename (`Launcher (1).exe`) breaks its self-update: the manifest entry is
`Launcher.exe`, so the renamed binary no longer recognises its own update and leaves a
second launcher next to itself. The file on the website is also stale.

## Goal

A small Windows setup, `AlestiaOnline-Setup.exe`, that lets the player pick an install
folder, installs only `Launcher.exe` there, and hands over to the launcher, which then
downloads the game into that folder exactly as it does today.

Success criteria:

- A player can install per-user without a UAC prompt, into a folder of their choice.
- An all-users install into Program Files works, and the launcher can update it without
  elevation.
- The installed launcher is always named `Launcher.exe`, so self-update works no matter
  what the browser called the setup file.
- Uninstall removes the game files the launcher downloaded, and nothing else.
- Every `launcher-v*` release carries a setup built and smoke-tested in CI, reachable under
  a stable URL for the website.

## Out of scope

- Code signing (decided separately; a later step will sign setup and launcher locally with
  an OV certificate per launcher release).
- Launcher code changes, including the rename-robust self-update and decoupling the game
  directory from the exe location. Not needed for setup installs.
- Signing or verifying the client binaries the launcher downloads.
- Updating the website itself (the maintainer swaps the link).

## Design

### Tooling and location

- Inno Setup 6, script at `deploy/installer/setup.iss`.
- Version is passed in at build time: `iscc /DAppVersion=<X.Y.Z> /DLauncherExe=<path>
  /O<outdir> setup.iss`. The script has no hardcoded version.
- Fixed `AppId` GUID (generated once, committed), so a newer setup recognises and upgrades
  an existing install instead of creating a second one.
- `AppName=Alestia Online`, `AppPublisher=Kyoril`, `OutputBaseFilename=AlestiaOnline-Setup`.
- Languages: German and English (`compiler:Languages\German.isl`, `compiler:Default.isl`),
  picked from the system language.
- Icons: the launcher has no application icon today (`launcher.rc` embeds only PNGs), so
  setup and shortcuts use the defaults. A proper `.ico` for launcher and setup is a separate,
  later change.

### Install model

- `PrivilegesRequired=lowest`, `PrivilegesRequiredOverridesAllowed=dialog`: the setup opens
  with Inno's "Install for me only (recommended)" / "Install for all users" choice.
- `DefaultDirName={autopf}\Alestia Online`:
  - per-user → `%LOCALAPPDATA%\Programs\Alestia Online`, no UAC;
  - all users → `%ProgramFiles%\Alestia Online`, UAC.
- The directory page stays enabled (`DisableDirPage=no`); any folder is allowed.
- `[Dirs] Name: "{app}"; Permissions: users-modify` grants the Users group write access, so
  the launcher can patch an all-users install without elevation. It is harmless for
  per-user installs.
- `DisableProgramGroupPage=yes`; a single Start menu entry is created.

### What gets installed

- `[Files]`: only `Launcher.exe` into `{app}`, `Flags: ignoreversion`.
- `[Icons]`: Start menu `Alestia Online` → `{app}\Launcher.exe` with `WorkingDir: {app}`;
  desktop shortcut via a `desktopicon` task, checked by default.
- `[Run]`: "Launch Alestia Online" (`postinstall nowait skipifsilent`), unprivileged
  (`runasoriginaluser`) so an all-users install does not start the launcher elevated.
- The setup does not write to the registry beyond Inno's own uninstall entry
  (HKCU for per-user, HKLM for all users).

Reinstall/upgrade over an existing folder only replaces `Launcher.exe`; downloaded game
files are kept and the launcher patches them as usual.

`CloseApplications=yes` with `AppMutex` is not used (the launcher has no mutex); Inno's
Restart Manager handles a running `Launcher.exe` on upgrade and uninstall.

### Uninstall

Inno removes what it installed. In addition, `[UninstallDelete]` removes exactly what the
launcher puts into the game folder:

- `{app}\Data` (`filesandordirs`)
- `{app}\mmo_client.exe`, `{app}\mmo_error.exe`, `{app}\fmod.dll`
- `{app}\launcher.ini`
- `{app}\Launcher.exe.*` (self-update leftovers)
- `{app}\Logs`, `{app}\Config`, `{app}\Cache` (`filesandordirs`): created by the client
  in its working directory (`client_application.cpp` creates `Logs` and `Config`;
  `client_cache.cpp` writes `Cache/*.db`). `Config` holds the player's settings and key
  bindings; uninstall removes them like any full game uninstall does.
- `{app}` itself with `Type: dirifempty`

The whole `{app}` folder is never deleted recursively: a player who installed into an
existing folder such as `C:\Games` must not lose unrelated files. If `source.txt` gains a
new top-level file, this list must be extended. `deploy/patch/source.txt` gets a comment
pointing to `setup.iss` if the SFF format accepts comments; otherwise the note goes into
`docs/deployment.md` next to the patch-source description.

The launcher log under `Documents\MMORPG`, the launcher's content cache under
`%LOCALAPPDATA%\AlestiaOnline` and client crash reports in `%TEMP%` are left alone.

### Build and publish

In `.github/workflows/launcher-release.yml`, after "Package":

1. **Build setup**: ensure Inno Setup is present (`iscc` on PATH or at its default install
   path; otherwise `choco install innosetup -y`), then run `iscc` with the tag version and
   `bin/Release/Launcher.exe`, output `dist/AlestiaOnline-Setup.exe`.
2. **Smoke test** on the runner:
   - `AlestiaOnline-Setup.exe /VERYSILENT /SUPPRESSMSGBOXES /CURRENTUSER /NORESTART
     /DIR=<temp> /TASKS=""`, wait for exit, assert exit code 0;
   - assert `<temp>\Launcher.exe` exists and matches the built launcher's sha256;
   - create fake `Data\x.hpak`, `mmo_client.exe`, `launcher.ini` in `<temp>` to stand in
     for a launcher download;
   - run `<temp>\unins000.exe /VERYSILENT /SUPPRESSMSGBOXES`, wait for the uninstaller's
     child process (poll until `<temp>` is gone, bounded timeout);
   - assert `<temp>` no longer exists.
3. **Publish**: add `dist/AlestiaOnline-Setup.exe` to the `gh release create` asset list.

`launcher-release.json` and the deployer are unchanged: the deployer keeps pinning
`Launcher.exe` by sha256.

### Download URL

`https://github.com/Kyoril/mmo/releases/latest/download/AlestiaOnline-Setup.exe`

This resolves to the newest non-prerelease. Verified 2026-10-15: `launcher-v1.0.0` is the
only non-prerelease and is marked latest; nightlies and legacy `build-N` releases are
prereleases. Invariant: every release other than `launcher-v*` must stay a prerelease, or
this link breaks. The docs state this next to the nightly workflow's publish step.

The first setup appears with the next `launcher-v*` tag. Backfilling `launcher-v1.0.0` is
possible by building the setup locally and `gh release upload`, but not required.

### Local build

`deploy/installer/README.md` documents building the setup locally from a built launcher
(`iscc` command line), for testing the setup outside CI.

## Documentation

- `docs/versioning.md`, launcher release section: the release now also carries the setup.
- `docs/deployment.md`: the website download link and the "non-launcher releases stay
  prereleases" invariant.

## Testing

- CI smoke test above (install, file present, uninstall removes launcher-downloaded files
  and the folder).
- Manual check once before the first release, on a Windows machine:
  per-user install with default folder; install into a custom folder; all-users install into
  Program Files followed by a launcher run that patches without elevation; uninstall from
  "Apps & Features" in each case; upgrade by running a newer setup over an existing install.

## Risks

- **SmartScreen still warns** until the setup is signed; out of scope here, unchanged from
  today.
- **Uninstall list drift**: a new top-level file in the patch manifest is left behind on
  uninstall until the list is extended. Mitigated by the cross-reference note
  described under Uninstall.
- **`releases/latest` invariant**: a non-prerelease published by any other workflow would
  hijack the website link.
