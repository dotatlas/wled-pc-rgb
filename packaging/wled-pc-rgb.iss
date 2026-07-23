; wled-pc-rgb — Inno Setup installer script.
;
; Builds a standard Windows installer from the staged portable release. Run the
; portable packager first so core-cpp\build\app is fully populated (exe + DLL
; closure + WledBackend.java), then compile this with Inno Setup 6:
;
;   pwsh -File scripts\package-win.ps1
;   iscc packaging\wled-pc-rgb.iss
;
; Output: dist\wled-pc-rgb-setup-vX.Y.exe
;
; Note: the app orchestrates but does not bundle its two runtime prerequisites —
; a JDK (for WledBackend.java) and OpenRGB. See docs\USAGE.md. The installer only
; ships the self-contained app. Code signing (signtool) is a separate manual step
; and requires a certificate; it is intentionally not wired in here.

#define AppName    "wled-pc-rgb"
#define AppVersion "0.19"
#define AppExe     "wled_pc_rgb.exe"
#define AppPublisher "dotatlas"
#define SrcApp     "..\core-cpp\build\app"

[Setup]
AppId={{4C7B2E9A-3F1D-4E62-9C0A-WLEDPCRGB0019}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
; Prompt for the install location every time (user preference: choose where).
DisableDirPage=no
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=..\dist
OutputBaseFilename=wled-pc-rgb-setup-v{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern

[Files]
; The whole self-contained folder, minus CMake bookkeeping.
Source: "{#SrcApp}\*"; DestDir: "{app}"; Excludes: "cmake_install.cmake"; Flags: recursesubdirs ignoreversion
Source: "..\README.md";      DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\USAGE.md";  DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}";           Filename: "{app}\{#AppExe}"
Name: "{group}\Usage guide";          Filename: "{app}\USAGE.md"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";     Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"
Name: "startmin";    Description: "Launch at login (minimised to tray)"; GroupDescription: "Startup:"; Flags: unchecked

[Registry]
; Optional autostart — mirrors the in-app "Launch at login" option.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; \
    ValueName: "wled-pc-rgb"; ValueData: """{app}\{#AppExe}"" --minimized"; \
    Flags: uninsdeletevalue; Tasks: startmin

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName} now"; Flags: nowait postinstall skipifsilent
