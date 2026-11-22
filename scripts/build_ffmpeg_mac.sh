#!/bin/bash
# Builds a small ffmpeg for both Mac chip types (Apple Silicon and Intel) and joins them into one file.
# Only what the plugin needs is switched on: read a picture and a WAV/MP3/FLAC, draw the moving bars over it,
# write H.264 + AAC in MP4, and MP3.
# It is an LGPL build (no GPL parts), using macOS's own H.264 encoder, so it can ship next to the plugin.
set -e
cd "$(dirname "$0")/.."
FFMPEG_VERSION="7.1"
LAME_VERSION="3.100"
OUT="$PWD/ffmpeg-build"
WORK="$PWD/ffmpeg-work"
[ -f "$OUT/ffmpeg" ] && { echo "ffmpeg already built"; exit 0; }
rm -rf "$WORK"; mkdir -p "$OUT" "$WORK"; cd "$WORK"

curl -L --fail -o lame.tar.gz "https://downloads.sourceforge.net/project/lame/lame/$LAME_VERSION/lame-$LAME_VERSION.tar.gz"
curl -L --fail -o ffmpeg.tar.xz "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz"

for ARCH in arm64 x86_64; do
  PREFIX="$WORK/prefix-$ARCH"
  FLAGS="-arch $ARCH -mmacosx-version-min=11.0"
  if [ "$ARCH" = "arm64" ]; then HOST=aarch64-apple-darwin; else HOST=x86_64-apple-darwin; fi
  # The Intel build is made with no hand-written assembly at all. With it, turning the picture into video colours
  # went wrong (green and pink lines across the whole video), which is what Intel Macs and Logic under Rosetta use.
  if [ "$ARCH" = "arm64" ]; then ASM=""; else ASM="--disable-asm"; fi

  rm -rf "lame-$ARCH"; mkdir "lame-$ARCH"; tar -xzf lame.tar.gz -C "lame-$ARCH" --strip-components 1
  ( cd "lame-$ARCH"
    # newer compilers refuse a symbol the old export list still names
    sed -i '' '/lame_init_old/d' include/libmp3lame.sym
    CFLAGS="$FLAGS" LDFLAGS="$FLAGS" ./configure --prefix="$PREFIX" --host="$HOST" --disable-shared --enable-static --disable-frontend >/dev/null
    make -j4 >/dev/null && make install >/dev/null )

  rm -rf "ffmpeg-$ARCH"; mkdir "ffmpeg-$ARCH"; tar -xJf ffmpeg.tar.xz -C "ffmpeg-$ARCH" --strip-components 1
  ( cd "ffmpeg-$ARCH"
    ./configure --prefix="$PREFIX" --arch="$ARCH" --target-os=darwin --enable-cross-compile --cc="clang -arch $ARCH" \
      --extra-cflags="$FLAGS -I$PREFIX/include" --extra-ldflags="$FLAGS -L$PREFIX/lib" \
      --disable-everything --disable-doc --disable-ffplay --disable-ffprobe --disable-network --disable-autodetect \
      --disable-debug --disable-x86asm $ASM --enable-small \
      --enable-videotoolbox --enable-audiotoolbox --enable-libmp3lame \
      --enable-protocol=file \
      --enable-demuxer=image2,png_pipe,mjpeg,wav,mp3,aiff,flac,rawvideo \
      --enable-decoder=png,mjpeg,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_s16be,pcm_s24be,mp3,flac,rawvideo \
      --enable-parser=png,mjpeg,mpegaudio,flac \
      --enable-encoder=h264_videotoolbox,mpeg4,aac,libmp3lame \
      --enable-muxer=mp4,mp3 \
      --enable-filter=scale,format,aresample,afade,null,anull,overlay,alphamerge,color \
      --enable-swscale --enable-swresample --enable-zlib >/dev/null
    make -j4 >/dev/null )
  cp "ffmpeg-$ARCH/ffmpeg" "ffmpeg-$ARCH.bin"
done

lipo -create ffmpeg-arm64.bin ffmpeg-x86_64.bin -output "$OUT/ffmpeg"
strip "$OUT/ffmpeg" || true
cp ffmpeg-arm64/COPYING.LGPLv2.1 "$OUT/LICENSE.txt"
cd .. && rm -rf "$WORK"
"$OUT/ffmpeg" -hide_banner -encoders | grep -E "h264_videotoolbox|libmp3lame| aac " 
lipo -info "$OUT/ffmpeg"
