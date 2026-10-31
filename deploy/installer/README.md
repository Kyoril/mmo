# Client setup

`setup.iss` builds `AlestiaOnline-Setup.exe` (Inno Setup 6). The setup installs only
`Launcher.exe` into a folder the player picks; the launcher then downloads the game there.
Per-user install (`%LOCALAPPDATA%\Programs\Alestia Online`, no UAC) is the default; "all users"
installs to Program Files and makes the folder writable for Users so the launcher can patch it.

Design: [docs/superpowers/specs/2026-10-15-client-setup-design.md](../../docs/superpowers/specs/2026-10-15-client-setup-design.md)

## Look

The setup is always dark (`WizardStyle=modern dark`, not following the system theme) on the
launcher's brown (`WizardBackColor`, also used by the uninstaller), with a darkened crop of the
launcher splash behind every page, the castle on the welcome and finish pages and a spire detail
top right. The art lives in `art/` as PNG sets for 100/150/200 % DPI and was made by hand from
`src/launcher/res/splash.png`; there is no generator in the repo, so regenerate it by hand if the
splash changes (keep the sizes listed in the Inno help for `WizardBackImageFile`,
`WizardImageFile` and `WizardSmallImageFile`).

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

# macOS disk image

`macos/build_dmg.sh` builds `AlestiaOnline.dmg`. Like the setup it installs only the launcher:
the image holds an `Alestia Online` folder with `Launcher.app` and a link to Applications. The
player drags the folder to Applications, opens it and starts the launcher, which downloads the
game into that folder (the client looks for its Data, Config and Logs next to `mmo_client.app`
the same way). Uninstalling is dragging the folder to the Trash.

The folder, not the bare app, is what gets dragged, because the launcher installs the game next
to itself. A bare `Launcher.app` in Applications would fill `/Applications` with game files.

## Build locally

macOS with Xcode's command line tools, Python 3 and a built launcher bundle:

```bash
dmg=$(deploy/installer/macos/build_dmg.sh --version 1.0.1 --launcher bin/Launcher.app)
deploy/installer/macos/smoke_test_dmg.sh --dmg "$dmg" --version 1.0.1 --launcher bin/Launcher.app
```

`--version` must match the bundle's `CFBundleShortVersionString`, which comes from the newest
`launcher-v` tag. The DMG lands in `dist/` unless `--out-dir` says otherwise. `dmgbuild` (pinned
in the script) writes the Finder window layout without Finder, so CI needs no GUI session. The
window art is rendered at build time by `macos/make_background.swift` from the launcher's splash
and display font; its icon positions must match `macos/dmg_settings.py`.

The smoke test verifies and mounts the image, checks its layout and version, and starts the
launcher from the read-only image and from a copied install folder (`--render-preview`).

## Gatekeeper

Until the DMG is signed and notarized, macOS 15 refuses to open the launcher the first time:
the player opens it once, then confirms under System Settings > Privacy & Security >
"Open Anyway". Because the app arrives inside a folder, macOS then runs it from a randomized
read-only copy ("App Translocation"). The launcher detects that, clears the quarantine flag of
its real copy and restarts from there, so the game is installed into the folder in Applications.
Started straight from the mounted image (or any folder it cannot write to), the launcher asks
the player to drag the folder to Applications first.

Signing switches on in CI once these repository secrets exist; nothing else changes:

| Secret | Content |
|---|---|
| `MACOS_CERTIFICATE_P12` | base64 of the exported "Developer ID Application" certificate with its key |
| `MACOS_CERTIFICATE_PASSWORD` | password of that .p12 |
| `MACOS_NOTARY_KEY` | App Store Connect API key (.p8 contents) for notarization |
| `MACOS_NOTARY_KEY_ID`, `MACOS_NOTARY_ISSUER` | that key's id and issuer id |

Locally, `MACOS_SIGN_IDENTITY` (and `MACOS_NOTARY_PROFILE`, a `notarytool store-credentials`
profile) turn on the same steps in `build_dmg.sh`.

## Rules (macOS)

- The DMG is arm64 only and needs the macOS version of the build runner (`macos-15`) or newer.
- Keep the bundle name `Launcher.app`: launcher self-update finds itself in the patch manifest by
  its path inside the install folder.
- The game patch has no macOS client yet (`deploy/patch/source.txt` ships the client under
  `WINDOWS` only), so until it does, a Mac install downloads the game data but cannot start the
  game.
