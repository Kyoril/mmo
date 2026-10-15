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
