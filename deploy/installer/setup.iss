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
; Program Files needs the folder writable for Users. Only there: a per-user install lives in
; the player's profile, and Users-Modify on it would let any other local account replace the
; player's Launcher.exe. The uninstaller files are locked down again in CurStepChanged below.
Name: "{app}"; Permissions: users-modify; Check: IsAdminInstallMode

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

[CustomMessages]
en.InstallIntoSubfolder=The selected folder is not empty. Alestia Online will be installed into its own subfolder:%n%n%1%n%nClick Next to continue.
de.InstallIntoSubfolder=Der angegebene Ordner ist nicht leer. Alestia Online wird in einen eigenen Unterordner installiert:%n%n%1%n%nKlicken Sie auf Weiter, um fortzufahren.
en.UninstallerLockdownFailed=Setup could not restrict access to the uninstaller (icacls exit code %1), so other users could replace it. Please uninstall Alestia Online and run Setup again.
de.UninstallerLockdownFailed=Setup konnte den Zugriff auf das Deinstallationsprogramm nicht einschraenken (icacls-Rueckgabewert %1), andere Benutzer koennten es ersetzen. Bitte deinstallieren Sie Alestia Online und fuehren Sie Setup erneut aus.

[Code]
var
	UninstallerLockdownFailed: Boolean;

// True if Dir contains at least one file or folder.
function DirHasEntries(const Dir: String): Boolean;
var
	FindRec: TFindRec;
	More: Boolean;
begin
	Result := False;
	if FindFirst(AddBackslash(Dir) + '*', FindRec) then
	begin
		try
			More := True;
			while More and not Result do
			begin
				if (FindRec.Name <> '.') and (FindRec.Name <> '..') then
				begin
					Result := True;
				end;
				More := FindNext(FindRec);
			end;
		finally
			FindClose(FindRec);
		end;
	end;
end;

// Uninstall deletes Data, Logs, Config and Cache recursively. In a folder that already held
// other things (C:\Games, say) those names may belong to someone else, so a non-empty folder
// that is not an Alestia Online install (no Launcher.exe, no unins000.dat) gets its own
// subfolder. The dir page stays open with the new path filled in (return False) instead of
// moving on: the player sees exactly where the game goes and can still change it. The next
// click on Next passes, because the subfolder is new, empty or itself an install.
// Silent installs keep /DIR as given: NextButtonClick is simulated there too, but nobody could
// read the message, and a scripted /DIR means what it says.
function NextButtonClick(CurPageID: Integer): Boolean;
var
	Dir: String;
	SubDir: String;
begin
	Result := True;
	if (CurPageID = wpSelectDir) and not WizardSilent then
	begin
		Dir := WizardDirValue;
		if DirExists(Dir) and DirHasEntries(Dir)
			and not FileExists(AddBackslash(Dir) + 'Launcher.exe')
			and not FileExists(AddBackslash(Dir) + 'unins000.dat') then
		begin
			SubDir := AddBackslash(Dir) + 'Alestia Online';
			WizardForm.DirEdit.Text := SubDir;
			MsgBox(FmtMessage(CustomMessage('InstallIntoSubfolder'), [SubDir]), mbInformation, MB_OK);
			Result := False;
		end;
	end;
end;

// An all-users install makes {app} Users-Modify (see [Dirs]), and the uninstaller files inherit
// that. Apps & Features runs the uninstaller elevated, so a standard user who swapped
// unins000.exe or edited unins000.dat would get admin. Replace their inherited ACL with
// Administrators and SYSTEM Full, Users Read & Execute.
//
// ssPostInstall is the right step: the help topic "Installation Order" lists "The uninstaller
// EXE and log are finalized and saved to disk" as the last install step, and "Pascal Scripting:
// Event Functions" says CurStepChanged(ssPostInstall) is called "just after the actual
// installation finishes" (ssDone only comes just before Setup terminates). Every install,
// upgrades included, passes through here, so a rewritten unins000.dat is locked down again.
//
// A failure must not look like a successful install, but raising an exception here does not do
// that: Setup only logs exceptions from CurStepChanged and still exits 0 (checked with Inno
// Setup 6.7.3), and nothing is rolled back once the uninstaller is saved. So log, show a
// critical message (suppressed by /SUPPRESSMSGBOXES) and make GetCustomSetupExitCode return 4,
// Inno's "fatal error during the actual installation process".
function LockDownFile(const FileName: String): Boolean;
var
	ResultCode: Integer;
begin
	Result := True;
	if not FileExists(FileName) then
	begin
		exit;
	end;
	if not Exec(ExpandConstant('{sys}\icacls.exe'),
		'"' + FileName + '" /inheritance:r /grant:r *S-1-5-32-544:F *S-1-5-18:F *S-1-5-32-545:RX',
		'', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
	begin
		Log(Format('icacls could not be started for %s: %s', [FileName, SysErrorMessage(ResultCode)]));
		ResultCode := -1;
	end;
	if ResultCode <> 0 then
	begin
		Log(Format('Locking down %s failed, icacls exit code %d', [FileName, ResultCode]));
		SuppressibleMsgBox(FmtMessage(CustomMessage('UninstallerLockdownFailed'), [IntToStr(ResultCode)]), mbCriticalError, MB_OK, IDOK);
		Result := False;
	end
	else
	begin
		Log(Format('Locked down %s', [FileName]));
	end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
	UninstallExe: String;
begin
	if (CurStep = ssPostInstall) and IsAdminInstallMode then
	begin
		UninstallExe := ExpandConstant('{uninstallexe}');
		if not LockDownFile(UninstallExe) then
		begin
			UninstallerLockdownFailed := True;
		end
		else if not LockDownFile(ChangeFileExt(UninstallExe, '.dat')) then
		begin
			UninstallerLockdownFailed := True;
		end;
	end;
end;

function GetCustomSetupExitCode: Integer;
begin
	Result := 0;
	if UninstallerLockdownFailed then
	begin
		Result := 4;
	end;
end;
