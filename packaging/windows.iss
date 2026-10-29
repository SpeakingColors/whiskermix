; WhiskerMix Windows installer + uninstaller (Inno Setup). Called as: ISCC /DName=WhiskerMix /DProduct=WhiskerMix /DVersion=0.2.0 packaging\windows.iss
; Files are staged in ..\stage by the release workflow. Works offline. The uninstaller removes everything it installed;
; your workspaces and snapshots (%APPDATA%\WhiskerMix) are kept unless you answer Yes when it asks.
[Setup]
AppName={#Product}
AppVersion={#Version}
AppPublisher=Studio 87
AppPublisherURL=https://github.com/SpeakingColors/whiskermix
DefaultDirName={autopf}\Studio 87\{#Product}
DefaultGroupName=Studio 87
OutputDir=..\Output
OutputBaseFilename={#Name}-Setup
LicenseFile=..\LICENSE
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
UninstallDisplayName={#Product}
UninstallDisplayIcon={app}\{#Product}.exe
CloseApplications=yes

[Files]
Source: "..\stage\{#Product}.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\stage\{#Name}-manual.pdf"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\stage\INSTALL.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\stage\vst3\*"; DestDir: "{commoncf64}\VST3"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "..\stage\standalone\*"; DestDir: "{app}\plugins"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\{#Product}"; Filename: "{app}\{#Product}.exe"
Name: "{group}\{#Product} manual"; Filename: "{app}\{#Name}-manual.pdf"
Name: "{group}\Uninstall {#Product}"; Filename: "{uninstallexe}"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\ShimmerDelay.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\ShimmerReverb.vst3"
Type: filesandordirs; Name: "{app}"

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var d: String;
begin
  if CurUninstallStep = usPostUninstall then begin
    d := ExpandConstant('{userappdata}\WhiskerMix');
    if DirExists(d) then
      if (not UninstallSilent) and (MsgBox('Also delete your WhiskerMix workspaces, snapshots and settings?' + #13#10 + d, mbConfirmation, MB_YESNO) = IDYES) then
        DelTree(d, True, True, True);
  end;
end;
