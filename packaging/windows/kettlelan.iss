; KettleLAN – Windows installer (Inno Setup 6). Built by packaging/windows/build-installer.sh:
;   ISCC.exe /DAppVersion=0.3.0 /DSourceDir=<folder with the program> /DOutputDir=<folder> kettlelan.iss
;
; Installs KettleLAN to Program Files, installs and starts "KettleLAN Service" (the Wintun adapter,
; runs as SYSTEM) and adds a Start menu shortcut. The program itself runs as the user.
; Uninstalling removes the service, its firewall rules and the shortcuts, and keeps your saved
; networks and settings (in your user registry), so a reinstall loses nothing.

#ifndef AppVersion
  #error AppVersion is not set
#endif

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
; the autostart option writes the user's own "Run" key on purpose
UsedUserAreasWarning=no

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
Name: "autostart"; Description: "Start KettleLAN with Windows"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\KettleLAN"; Filename: "{app}\KettleLAN.exe"
Name: "{autodesktop}\KettleLAN"; Filename: "{app}\KettleLAN.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "KettleLAN"; \
    ValueData: """{app}\KettleLAN.exe"" --minimized"; Tasks: autostart; Flags: uninsdeletevalue

[Run]
Filename: "{app}\kettlelan-service.exe"; Parameters: "install"; Flags: runhidden waituntilterminated; \
    StatusMsg: "Installing KettleLAN Service..."
Filename: "{app}\KettleLAN.exe"; Description: "Launch KettleLAN"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM KettleLAN.exe"; Flags: runhidden waituntilterminated; RunOnceId: "CloseKettleLAN"
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM kettlelan-steam.exe"; Flags: runhidden waituntilterminated; RunOnceId: "CloseKettleLANSteam"
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM kettlelan-cli.exe"; Flags: runhidden waituntilterminated; RunOnceId: "CloseKettleLANCli"
Filename: "{app}\kettlelan-service.exe"; Parameters: "uninstall"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveService"

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
