; Inno Setup script for Catch Chart Editor.
; All paths are relative to this script's directory so the build is reproducible
; on any machine that has the repository and Inno Setup 6 installed.
;
; Typical invocation (see tools/packaging/build_and_package.ps1):
;   ISCC.exe /DMyAppVersion=1.11.2 /DMyDisplayVersion="Beta v1.11.2" ^
;            /DMyBuildDir=<repo>\build\Release /DMyOutputDir=<dist> ^
;            tools\packaging\setup.iss

#define MyAppName "Catch Chart Editor"
#define MyAppPublisher "ChuanYuan"
#define MyAppURL "https://github.com/ChuanYuanNotBoat/Malody_Catch_Editor"
#define MyAppExeName "CatchChartEditor.exe"
#define MyAppAssocName "Malody Chart file"
#define MyAppAssocExt ".mc"
#define MyAppAssocKey StringChange(MyAppAssocName, " ", "") + MyAppAssocExt

#ifndef MyAppVersion
  #define MyAppVersion "1.11.2"
#endif
#ifndef MyDisplayVersion
  #define MyDisplayVersion "Beta v" + MyAppVersion
#endif
; Setup file name must stay space free, e.g. CatchChartEditor_Beta_v1.11.2_Setup.
#define MyTag StringChange(MyDisplayVersion, " ", "_")
#ifndef MyBuildDir
  #define MyBuildDir "..\..\build\Release"
#endif
#ifndef MyOutputDir
  #define MyOutputDir "..\..\dist"
#endif

[Setup]
AppId={{8F3269E2-0AC6-4AE6-A99A-1F8AF5C29E30}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyDisplayVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
OutputDir={#MyOutputDir}
OutputBaseFilename=CatchChartEditor_{#MyTag}_Setup
SetupIconFile=icons\4.ico
SolidCompression=yes
WizardStyle=modern dynamic

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "icons\1.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "icons\2.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "icons\3.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "icons\4.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyBuildDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; Ship the deployed runtime tree, but never test executables, debug symbols,
; the VC redist bootstrapper, local logs, or diagnostic dumps.
; Excludes use patterns so newly added test targets are covered too.
Source: "{#MyBuildDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*Tests.*,CatchChartEditorTests.*,autotiming_*_tests.*,autotiming_probe.*,CatchChartEditorTests.exe,CoordMapperTests.*,DockingLayoutTests.*,MetaEditPanelTests.*,ImageConverterTests.*,BpmMeasurementTests.*,*.pdb,*.ilk,vc_redist.x64.exe,tree.txt,logs/*,*.log"

[Dirs]
Name: "{app}\beatmap"

[Registry]
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocExt}\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppAssocKey}"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}"; ValueType: string; ValueName: ""; ValueData: "{#MyAppAssocName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\\1.ico"
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\1.ico"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; IconFilename: "{app}\1.ico"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
