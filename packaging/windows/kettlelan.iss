; KettleLAN – Windows installer (Inno Setup 6). Built by packaging/windows/build-installer.sh:
;   ISCC.exe /DAppVersion=0.3.0 /DSourceDir=<folder with the program> /DOutputDir=<folder> kettlelan.iss
;
; Installs KettleLAN to the folder the user picks (Program Files by default) and "KettleLAN Service"
; (the Wintun adapter, runs as SYSTEM) always to Program Files\KettleLAN Service, and adds a Start menu
; shortcut. The program itself runs as the user.
; Uninstalling removes the service, its firewall rules and the shortcuts, and keeps your saved
; networks and settings (in your user registry), so a reinstall loses nothing.

#ifndef AppVersion
  #error AppVersion is not set
#endif

; The service runs as SYSTEM, so it always goes to a folder only administrators can change.
#define ServiceDir "{commonpf64}\KettleLAN Service"

[Setup]
AppId={{8F3C2D6A-4B7E-4C1D-9A5E-2E6B7C0D1F34}
AppName=KettleLAN
AppVersion={#AppVersion}
AppVerName=KettleLAN {#AppVersion}
AppPublisher=KettleLAN (open source)
AppPublisherURL=https://github.com/maxiuz94/kettlelan
AppSupportURL=https://github.com/maxiuz94/kettlelan/issues
AppUpdatesURL=https://github.com/maxiuz94/kettlelan/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\KettleLAN
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile={#SourceDir}\LICENSE
SetupIconFile=kettlelan.ico
UninstallDisplayIcon={app}\KettleLAN.exe
UninstallDisplayName=KettleLAN
OutputDir={#OutputDir}
OutputBaseFilename=KettleLAN-{#AppVersion}-windows-x64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; KettleLAN.exe is closed by Inno Setup's Restart Manager support; the service is stopped below
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "kettlelan-service.exe,wintun.dll"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\kettlelan-service.exe"; DestDir: "{#ServiceDir}"; Flags: ignoreversion
Source: "{#SourceDir}\wintun.dll"; DestDir: "{#ServiceDir}"; Flags: ignoreversion

[InstallDelete]
; KettleLAN 0.3.0 kept the service in the program's folder
Type: files; Name: "{app}\kettlelan-service.exe"
Type: files; Name: "{app}\wintun.dll"

[Icons]
Name: "{autoprograms}\KettleLAN"; Filename: "{app}\KettleLAN.exe"
Name: "{autodesktop}\KettleLAN"; Filename: "{app}\KettleLAN.exe"; Tasks: desktopicon

[Run]
Filename: "{#ServiceDir}\kettlelan-service.exe"; Parameters: "install ""{app}\KettleLAN.exe"""; Flags: runhidden waituntilterminated; \
    StatusMsg: "Installing KettleLAN Service..."
Filename: "{app}\KettleLAN.exe"; Description: "Launch KettleLAN"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM KettleLAN.exe"; Flags: runhidden waituntilterminated; RunOnceId: "CloseKettleLAN"
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM kettlelan-steam.exe"; Flags: runhidden waituntilterminated; RunOnceId: "CloseKettleLANSteam"
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM kettlelan-cli.exe"; Flags: runhidden waituntilterminated; RunOnceId: "CloseKettleLANCli"
Filename: "{#ServiceDir}\kettlelan-service.exe"; Parameters: "uninstall"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveService"

[UninstallDelete]
; the service log
Type: filesandordirs; Name: "{commonappdata}\KettleLAN"

[Code]
// Before files are replaced (update or repair): close KettleLAN and stop the service, which keeps its
// files open. Both are started again at the end of the installation.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM KettleLAN.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM kettlelan-steam.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM kettlelan-cli.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec(ExpandConstant('{sys}\net.exe'), 'stop KettleLAN', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := '';
end;
