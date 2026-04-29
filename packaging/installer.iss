#define MyAppName "PortProbeQt"
#define MyAppExeName "PortProbeQt.exe"
#define MyAppPublisher "Msg-Lbo"
#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif
#define MyAppURL "https://github.com/Msg-Lbo/port-probe"

#ifndef MyArch
  #define MyArch "x64"
#endif

[Setup]
AppId={{D3C5E3A4-1B5E-4F35-8A40-7DB6D2E0F6C1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
SetupIconFile=..\app.ico
DefaultDirName={pf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\installer_output
OutputBaseFilename=PortProbeQt_Setup_{#MyArch}_v{#MyAppVersion}
Compression=lzma
SolidCompression=yes
PrivilegesRequired=admin
#if "x64" == MyArch
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
#endif
ShowLanguageDialog=no
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: checkedonce

[Files]
Source: "..\dist\{#MyArch}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\app.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\app.ico"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\app.ico"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

