#!/bin/bash
# Packages the built AU + VST3 (and the video tool) into a double-click .pkg installer.
# Usage: bash scripts/make_installer.sh <artefacts dir> [build number]
set -e
cd "$(dirname "$0")/.."
ART="${1:-build/MUUploader_artefacts/Release}"
BUILD="${2:-local}"
BASE="$(grep -m1 'set(MUU_VERSION_BASE' CMakeLists.txt | sed -E 's/.*"([0-9.]+)".*/\1/')"
if [ "$BUILD" = "local" ]; then VERSION="$BASE.0"; else VERSION="$BASE.$BUILD"; fi
NAME="MU Uploader"

AU="$ART/AU/$NAME.component"
VST3="$ART/VST3/$NAME.vst3"
APP="$ART/Standalone/$NAME.app"
[ -d "$AU" ] || { echo "AU not found at $AU"; exit 1; }
[ -d "$VST3" ] || { echo "VST3 not found at $VST3"; exit 1; }

rm -rf dist pkgroot
mkdir -p dist pkgroot/Library/Audio/Plug-Ins/Components pkgroot/Library/Audio/Plug-Ins/VST3 "pkgroot/Library/Application Support/$NAME"
cp -R "$AU" pkgroot/Library/Audio/Plug-Ins/Components/
cp -R "$VST3" pkgroot/Library/Audio/Plug-Ins/VST3/
if [ -d "$APP" ]; then
  mkdir -p pkgroot/Applications
  cp -R "$APP" pkgroot/Applications/
fi

# The video tool that turns a beat and its cover into the YouTube video.
if [ -f ffmpeg-build/ffmpeg ]; then
  cp ffmpeg-build/ffmpeg "pkgroot/Library/Application Support/$NAME/ffmpeg"
  chmod 755 "pkgroot/Library/Application Support/$NAME/ffmpeg"
  [ -f ffmpeg-build/LICENSE.txt ] && cp ffmpeg-build/LICENSE.txt "pkgroot/Library/Application Support/$NAME/ffmpeg-LICENSE.txt"
  codesign --force --sign - "pkgroot/Library/Application Support/$NAME/ffmpeg" || true
else
  echo "WARNING: no ffmpeg was built; this installer cannot make videos"
fi

# Ad-hoc sign so Gatekeeper is happier on Apple Silicon (replace with a Developer ID later).
codesign --force --deep --sign - "pkgroot/Library/Audio/Plug-Ins/Components/$NAME.component" || true
codesign --force --deep --sign - "pkgroot/Library/Audio/Plug-Ins/VST3/$NAME.vst3" || true
[ -d "pkgroot/Applications/$NAME.app" ] && codesign --force --deep --sign - "pkgroot/Applications/$NAME.app" || true

pkgbuild --root pkgroot --identifier com.musicallyunorthodox.uploader.pkg --version "$VERSION" \
         --install-location / "dist/MUUploader-$VERSION-build$BUILD.pkg"
cp "dist/MUUploader-$VERSION-build$BUILD.pkg" dist/MUUploader-Mac.pkg
rm -rf pkgroot
echo "Installer written to dist/"
ls -la dist
