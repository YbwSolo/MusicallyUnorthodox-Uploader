# Builds the Windows plugin (VST3 + standalone), runs the tests and makes the installer.
param([string]$BuildNumber = "0")
$ErrorActionPreference = "Stop"

$base = (Select-String -Path CMakeLists.txt -Pattern 'set\(MUU_VERSION_BASE "([^"]+)"\)').Matches[0].Groups[1].Value
$version = "$base.$BuildNumber"

# The video tool: an LGPL ffmpeg build for Windows (H.264 through Windows' own encoder, MP3 through LAME).
New-Item -ItemType Directory -Force ffmpeg-build | Out-Null
if (!(Test-Path ffmpeg-build\ffmpeg.exe)) {
    $zip = "ffmpeg-build\ffmpeg.zip"
    Invoke-WebRequest -Uri "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-lgpl.zip" -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath ffmpeg-build\unpacked -Force
    $exe = Get-ChildItem -Recurse ffmpeg-build\unpacked -Filter ffmpeg.exe | Select-Object -First 1
    Copy-Item $exe.FullName ffmpeg-build\ffmpeg.exe
    $licence = Get-ChildItem -Recurse ffmpeg-build\unpacked -Filter LICENSE.txt | Select-Object -First 1
    if ($licence) { Copy-Item $licence.FullName ffmpeg-build\ffmpeg-LICENSE.txt }
    Remove-Item -Recurse -Force ffmpeg-build\unpacked, $zip
}
& ffmpeg-build\ffmpeg.exe -hide_banner -version | Select-Object -First 1

cmake -B build -G "Visual Studio 17 2022" -A x64 -DMUU_BUILD_TESTS=ON "-DMUU_BUILD_NUMBER=$BuildNumber" `
      "-DMUU_GOOGLE_CLIENT_ID=$env:MUU_GOOGLE_CLIENT_ID" "-DMUU_GOOGLE_CLIENT_SECRET=$env:MUU_GOOGLE_CLIENT_SECRET"
if ($LASTEXITCODE -ne 0) { throw "configure failed" }

cmake --build build --config Release --target MUUploader_VST3 MUUploader_Standalone MUUTests --parallel
if ($LASTEXITCODE -ne 0) { throw "build failed" }

# the tests look for ffmpeg where the installer will put it
$shared = Join-Path $env:ProgramData "MU Uploader"
New-Item -ItemType Directory -Force $shared | Out-Null
Copy-Item ffmpeg-build\ffmpeg.exe (Join-Path $shared "ffmpeg.exe") -Force
$tests = Get-ChildItem -Recurse build -Filter MUUTests.exe | Select-Object -First 1
& $tests.FullName
if ($LASTEXITCODE -ne 0) { throw "tests failed" }

$artefacts = Join-Path $PWD "build\MUUploader_artefacts\Release"
if (!(Test-Path (Join-Path $artefacts "VST3\MU Uploader.vst3"))) { throw "the VST3 was not built where expected" }

$iscc = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
if (!(Test-Path $iscc)) { choco install innosetup -y --no-progress; if ($LASTEXITCODE -ne 0) { throw "could not install Inno Setup" } }
New-Item -ItemType Directory -Force dist | Out-Null
& $iscc "/DAppVersion=$version" "/DArtefacts=$artefacts" "/DFFmpeg=$(Join-Path $PWD 'ffmpeg-build')" "/O$(Join-Path $PWD 'dist')" scripts\windows_installer.iss
if ($LASTEXITCODE -ne 0) { throw "installer failed" }
Get-ChildItem dist
