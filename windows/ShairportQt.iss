; ShairportQt Inno Setup installer script
; Requires Inno Setup 6+ (https://jrsoftware.org/isinfo.php)
;
; How to build the installer:
;   1. Build ShairportQt.exe with CMake (Release configuration).
;   2. Copy ShairportQt.exe into the same directory as this .iss file,
;      OR set the SourceDir define below to your build output directory.
;   3. Open this file in the Inno Setup IDE and click Build > Compile,
;      OR run: iscc.exe windows\ShairportQt.iss
;   The installer will be written to windows\Output\ShairportQt_Setup_<version>.exe

#define AppName      "ShairportQt"
#define AppVersion   "1.0.0.5"
#define AppPublisher "Frank Friemel"
#define AppURL       "https://github.com/Frank-Friemel/ShairportQt"
#define AppExeName   "ShairportQt.exe"

; Directory that contains the built ShairportQt.exe.
; Adjust this path if you build to a different location.
#define SourceDir    "."
; Icon shipped with the repo (relative to this .iss file)
#define IconFile     "..\res\ShairportQt.ico"

[Setup]
AppId={{E7A3B5C2-1F4D-4A8E-9B6C-3D2F1E0A7B94}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
AllowNoIcons=yes
OutputDir=Output
OutputBaseFilename=ShairportQt_Setup_{#AppVersion}
SetupIconFile={#IconFile}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
UninstallDisplayIcon={app}\{#AppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german";  MessagesFile: "compiler:Languages\German.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "startupicon"; Description: "Launch {#AppName} at Windows startup"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; Install the icon so shortcuts remain valid after uninstall of Start Menu items
Source: "{#IconFile}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}";                   Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\ShairportQt.ico"; IconIndex: 0
Name: "{group}\Uninstall {#AppName}";         Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";             Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\ShairportQt.ico"; IconIndex: 0; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#AppName}"; ValueData: """{app}\{#AppExeName}"""; Tasks: startupicon; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
// ---------------------------------------------------------------------------
// Check whether Apple Bonjour is installed.
// ShairportQt requires the Bonjour service (mDNSResponder) to discover
// AirPlay devices on the local network.
// ---------------------------------------------------------------------------
function BonjourInstalled: Boolean;
var
  ServicePath: String;
begin
  Result := RegQueryStringValue(HKLM,
    'SYSTEM\CurrentControlSet\Services\Bonjour Service',
    'ImagePath', ServicePath);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usUninstall then
    Exec('taskkill.exe', '/f /im {#AppExeName}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  BonjourURL: String;
  Answer: Integer;
begin
  if CurStep = ssPostInstall then
  begin
    if not BonjourInstalled then
    begin
      BonjourURL := 'https://support.apple.com/kb/DL999';
      Answer := MsgBox(
        '{#AppName} requires Apple Bonjour to discover AirPlay devices.' + #13#10 +
        'Bonjour does not appear to be installed on this machine.' + #13#10 + #13#10 +
        'Would you like to open the Apple Bonjour download page now?',
        mbConfirmation, MB_YESNO);
      if Answer = IDYES then
        ShellExec('open', BonjourURL, '', '', SW_SHOWNORMAL, ewNoWait, Answer);
    end;
  end;
end;
