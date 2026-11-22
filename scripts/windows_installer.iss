; MU Uploader for Windows: the VST3 plugin, the standalone app and the video tool.
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef Artefacts
  #define Artefacts "..\build\MUUploader_artefacts\Release"
#endif
#ifndef FFmpeg
  #define FFmpeg "..\ffmpeg-build"
#endif

[Setup]
AppId={{B8E4F2A7-3C19-4D5B-8F60-2A7D9C41E5B3}
AppName=MU Uploader
AppVersion={#AppVersion}
AppPublisher=Musically Unorthodox
AppPublisherURL=https://musicallyunorthodox.com
DefaultDirName={autopf}\MU Uploader
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputBaseFilename=MUUploader-Windows-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=MU Uploader
CloseApplications=no

[Messages]
WelcomeLabel2=This installs the MU Uploader plugin (VST3) and the standalone app.%n%nClose your music software first. When it has finished, open your music software again and rescan plugins if it does not show up.

[Files]
Source: "{#Artefacts}\VST3\MU Uploader.vst3\*"; DestDir: "{commoncf64}\VST3\MU Uploader.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\Standalone\MU Uploader.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#FFmpeg}\ffmpeg.exe"; DestDir: "{commonappdata}\MU Uploader"; Flags: ignoreversion
Source: "{#FFmpeg}\ffmpeg-LICENSE.txt"; DestDir: "{commonappdata}\MU Uploader"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\MU Uploader"; Filename: "{app}\MU Uploader.exe"
