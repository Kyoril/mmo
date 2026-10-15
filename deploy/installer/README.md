# Client setup

`setup.iss` builds `AlestiaOnline-Setup.exe` (Inno Setup 6). The setup installs only
`Launcher.exe` into a folder the player picks; the launcher then downloads the game there.
Per-user install (`%LOCALAPPDATA%\Programs\Alestia Online`, no UAC) is the default; "all users"
installs to Program Files and makes the folder writable for Users so the launcher can patch it.

Design: [docs/superpowers/specs/2026-10-15-client-setup-design.md](../../docs/superpowers/specs/2026-10-15-client-setup-design.md)

## Build locally

Install Inno Setup 6 once (`winget install --id JRSoftware.InnoSetup -e`), then:

```powershell
$setup = pwsh deploy/installer/build_setup.ps1 -Version 1.0.1 -Launcher bin/Release/Launcher.exe
pwsh deploy/installer/smoke_test.ps1 -Setup $setup -Launcher bin/Release/Launcher.exe
```

`build_setup.ps1` prints the ISCC output to the console and emits only the setup path, so the
`$setup = ...` line captures the path.

`-AllUsers` on the smoke test also checks an all-users install (elevated shell only).

The smoke test uses the real AppId. It aborts (exit 1) if a real Alestia Online install is
registered (HKCU, or HKLM with `-AllUsers`), so uninstall that install before running it.

## Rules

- Never change `AppId` in `setup.iss`: a new id makes upgrades install side by side.
- Uninstall removes only what the launcher and client put into the folder, listed one by one in
  `[UninstallDelete]`. A new top-level entry in `deploy/patch/source.txt` must be added there;
  `tools/tests/test_installer_uninstall_list.py` fails until it is.
- CI builds and smoke-tests the setup for every `launcher-v*` tag (`.github/workflows/launcher-release.yml`).
