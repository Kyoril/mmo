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
`$setup = ...` line captures the path. The setup lands in `dist/` unless `-OutDir` says otherwise.

`-AllUsers` on the smoke test also checks an all-users install (elevated shell only).

The smoke test uses the real AppId. It aborts (exit 1) if a real Alestia Online install is
registered (HKCU, or HKLM with `-AllUsers`), so uninstall that install before running it.

## Rules

- Never change `AppId` in `setup.iss`: a new id makes upgrades install side by side.
- Uninstall removes only what the launcher and client put into the folder, listed one by one in
  `[UninstallDelete]`. A new top-level entry in `deploy/patch/source.txt` must be added there;
  `tools/tests/test_installer_uninstall_list.py` fails until it is.
- Uninstall also removes the client's `Logs`, `Config` (settings and key bindings) and `Cache`
  folders in the install folder.
- Only an all-users install makes the folder writable for Users. Its `unins000.exe` and
  `unins000.dat` are then locked down to Administrators/SYSTEM Full, Users Read & Execute
  (Apps & Features runs the uninstaller elevated); a failure to do so fails the setup with exit
  code 4. The `-AllUsers` smoke test checks both.
- A folder picked in the wizard that is not empty and is not an existing install gets an
  `Alestia Online` subfolder, so uninstall never deletes someone else's `Data` or `Config`.
  Silent installs use `/DIR` as given.
- Close the launcher and the game before uninstalling. Inno's uninstaller does not close
  running programs (the setup does, on upgrade, through Restart Manager); files in use are left
  behind or removed after a reboot.
- Running a setup with the other scope over an existing install (`/ALLUSERS` over a per-user
  install, or `/CURRENTUSER` over an all-users one) creates a second uninstall registration
  (HKCU and HKLM). The wizard prevents this: `UsePreviousPrivileges` (default yes) reuses the
  scope of the existing install and skips the scope question.
- CI builds and smoke-tests the setup for every `launcher-v*` tag (`.github/workflows/launcher-release.yml`).
