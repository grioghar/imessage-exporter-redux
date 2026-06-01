; Inno Setup script for the Windows installer. Compiled in CI by ISCC.exe.
; The workflow stages the windeployqt'd app into a "staging" folder next to this
; script, then runs: iscc installer.iss  (paths are relative to this file).

#define MyAppName "iMessage Exporter"
#define MyAppVersion "0.2.7"
#define MyAppExe "imessage-exporter-gui.exe"
#define IconFile "icon.ico"

[Setup]
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=imessage-exporter
#if FileExists(SourcePath + IconFile)
SetupIconFile={#IconFile}
#endif
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=dist
OutputBaseFilename=iMessage-Exporter-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
Source: "staging\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExe}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
