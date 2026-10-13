#ifndef SourceRoot
  #define SourceRoot AddBackslash(SourcePath) + ".."
#endif
#ifndef PayloadDir
  #define PayloadDir SourceRoot + "\dist\release-payload"
#endif
#ifndef AppVersion
  #define AppVersion "0.2.0-alpha"
#endif
#ifndef OutputDir
  #define OutputDir SourceRoot + "\dist\release"
#endif

[Setup]
AppId={{AF0719D3-CE51-4414-A419-4D7D93D796CC}
AppName=Plume
AppVersion={#AppVersion}
AppPublisher=Plume contributors
AppCopyright=Copyright (c) 2026 Plume contributors - MIT
DefaultDirName={localappdata}\Programs\Plume
DefaultGroupName=Plume
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible and not arm64
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
OutputDir={#OutputDir}
OutputBaseFilename=Plume-{#AppVersion}-Setup
SetupIconFile={#SourceRoot}\assets\plume.ico
UninstallDisplayIcon={app}\Plume.exe
LicenseFile={#SourceRoot}\LICENSE
InfoBeforeFile={#SourceRoot}\packaging\BEFORE-INSTALL.txt
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
VersionInfoVersion=0.2.0.0
VersionInfoDescription=Plume - Installation pour le compte utilisateur
CloseApplications=yes
RestartApplications=no
SetupMutex=Local\Plume.Setup
AppMutex=Local\PlumeWallpaper.V0.SingleInstance
DisableProgramGroupPage=yes
UninstallDisplayName=Plume
#ifdef SignedBuild
SignedUninstaller=yes
SignTool=PlumeSigning
#endif

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "Créer un raccourci sur le bureau"; Flags: unchecked
Name: "autostart"; Description: "Lancer Plume à l'ouverture de session"; Flags: unchecked
Name: "ffmpeg"; Description: "Installer FFmpeg pour l'import GPU NVIDIA (Internet requis, licence GPLv3)"; Check: NeedsFFmpeg

[Files]
Source: "{#PayloadDir}\Plume.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\THIRD-PARTY-NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{userprograms}\Plume"; Filename: "{app}\Plume.exe"; Check: not WizardNoIcons
Name: "{userdesktop}\Plume"; Filename: "{app}\Plume.exe"; Tasks: desktopicon; Check: not WizardNoIcons

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "PlumeWallpaperV0"; ValueData: """{app}\Plume.exe"""; Tasks: autostart; Flags: uninsdeletevalue

[Run]
Filename: "{app}\Plume.exe"; Description: "Lancer Plume"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\Plume.exe"; Parameters: "--quit"; Flags: runhidden; RunOnceId: "StopPlume"

[Code]
function NeedsFFmpeg(): Boolean;
var
  Packages: String;
  PackageEntry, VersionEntry: TFindRec;
begin
  Result := True;
  if FileSearch('ffmpeg.exe', GetEnv('PATH')) <> '' then begin
    Result := False;
    exit;
  end;
  Packages := ExpandConstant('{localappdata}\Microsoft\WinGet\Packages');
  if FindFirst(Packages + '\Gyan.FFmpeg*', PackageEntry) then begin
    try
      repeat
        if FindFirst(Packages + '\' + PackageEntry.Name + '\ffmpeg-*', VersionEntry) then begin
          try
            repeat
              if FileExists(Packages + '\' + PackageEntry.Name + '\' + VersionEntry.Name + '\bin\ffmpeg.exe') then begin
                Result := False;
                exit;
              end;
            until not FindNext(VersionEntry);
          finally
            FindClose(VersionEntry);
          end;
        end;
      until not FindNext(PackageEntry);
    finally
      FindClose(PackageEntry);
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  WingetPath, Arguments: String;
  ExitCode: Integer;
  Started: Boolean;
begin
  if (CurStep <> ssPostInstall) or not WizardIsTaskSelected('ffmpeg') or not NeedsFFmpeg() then
    exit;
  WingetPath := ExpandConstant('{localappdata}\Microsoft\WindowsApps\winget.exe');
  if not FileExists(WingetPath) then
    WingetPath := FileSearch('winget.exe', GetEnv('PATH'));
  if WingetPath = '' then begin
    Log('WinGet absent : FFmpeg non installe.');
    if not WizardSilent then
      MsgBox('Plume est installé. WinGet est absent : installez FFmpeg séparément en suivant le README. Les vidéos déjà compatibles restent lisibles.', mbInformation, MB_OK);
    exit;
  end;
  WizardForm.StatusLabel.Caption := 'Téléchargement et installation de FFmpeg via WinGet...';
  Arguments := 'install --id Gyan.FFmpeg --exact --source winget --scope user --silent --disable-interactivity --accept-source-agreements --accept-package-agreements';
  Started := Exec(WingetPath, Arguments, '', SW_HIDE, ewWaitUntilTerminated, ExitCode);
  if not Started or (ExitCode <> 0) then begin
    Log(Format('Installation FFmpeg non terminee, code %d.', [ExitCode]));
    if not WizardSilent then
      MsgBox('Plume est installé, mais FFmpeg n''a pas pu être installé. Consultez le README pour l''installer séparément. Aucun mode CPU n''a été activé.', mbInformation, MB_OK);
  end;
end;

