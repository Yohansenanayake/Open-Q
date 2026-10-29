#define Version Trim(FileRead(FileOpen("..\VERSION")))
#define ProjectName GetEnv('PROJECT_NAME')
#define ProductName GetEnv('PRODUCT_NAME')
#define Publisher GetEnv('COMPANY_NAME')
#ifndef InstallerArchitecture
  #define InstallerArchitecture "x64compatible"
#endif
#ifndef ArtifactName
  #define ArtifactName ProductName + "-" + Version + "-Windows"
#endif
#define Year GetDateTimeString("yyyy","","")

; 'Types': What get displayed during the setup
[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

; Components are used inside the script and can be composed of a set of 'Types'
[Components]
Name: "standalone"; Description: "Standalone application"; Types: full custom
Name: "vst3"; Description: "VST3 plugin"; Types: full custom
Name: "clap"; Description: "CLAP plugin"; Types: full custom

[Setup]
ArchitecturesInstallIn64BitMode={#InstallerArchitecture}
ArchitecturesAllowed={#InstallerArchitecture}
AppName={#ProductName}
OutputBaseFilename={#ArtifactName}
AppCopyright=Copyright (C) {#Year} {#Publisher}
AppPublisher={#Publisher}
AppVersion={#Version}
DefaultDirName="{commoncf64}\VST3\{#ProductName}.vst3"
DisableDirPage=yes

; Use the project license rather than the template commercial EULA.
LicenseFile="..\LICENSE"
UninstallFilesDir="{commonappdata}\{#ProductName}\uninstall"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\{#ProductName}Data"

; MSVC adds a .ilk when building the plugin. Let's not include that.
[Files]
Source: "..\LICENSE"; DestDir: "{commonappdata}\{#ProductName}"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{commonappdata}\{#ProductName}"; Flags: ignoreversion
Source: "..\licenses\*"; DestDir: "{commonappdata}\{#ProductName}\licenses"; Flags: ignoreversion
Source: "..\Builds\{#ProjectName}_artefacts\Release\VST3\{#ProductName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#ProductName}.vst3\"; Excludes: *.ilk; Flags: ignoreversion recursesubdirs; Components: vst3
Source: "..\Builds\{#ProjectName}_artefacts\Release\CLAP\{#ProductName}.clap"; DestDir: "{commoncf64}\CLAP\"; Flags: ignoreversion; Components: clap
Source: "..\Builds\{#ProjectName}_artefacts\Release\Standalone\{#ProductName}.exe"; DestDir: "{commonpf64}\{#Publisher}\{#ProductName}"; Flags: ignoreversion; Components: standalone

[Icons]
Name: "{autoprograms}\{#ProductName}"; Filename: "{commonpf64}\{#Publisher}\{#ProductName}\{#ProductName}.exe"; Components: standalone
Name: "{autoprograms}\Uninstall {#ProductName}"; Filename: "{uninstallexe}"

; This is optional, for preset or other plugin data
[Run]
Filename: "{cmd}"; \
    WorkingDir: "{commoncf64}\VST3"; \
    Parameters: "/C mklink /D ""{commoncf64}\VST3\{#ProductName}Data"" ""{commonappdata}\{#ProductName}"""; \
    Flags: runascurrentuser; Components: vst3
