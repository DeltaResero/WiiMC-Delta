#!/bin/sh
# Regenerate config.h and config.mak for WiiMC with MPlayer's configure.
#
# WiiMC's build does not run configure; it uses the two files this script
# writes, which are committed. Run it only to regenerate them, from
# src/mplayer, after FFmpeg and libiconv have been built once by the
# top-level Makefile (configure checks against them):
#
#   DEVKITPRO=/opt/devkitpro ./configure-wii.sh
#
# configure has no bare-metal target. --target=ppc-linux only picks POSIX
# defaults, which config.h then overrides for the Wii where it says GEKKO.
# Absolute paths in the output are replaced by the make variables the
# top-level Makefile exports, so the result does not depend on where the
# tree or devkitPro live.
set -eu
: "${DEVKITPRO:=/opt/devkitpro}"
HERE=$(cd "$(dirname "$0")" && pwd)
EXTPREFIX=${EXTPREFIX:-$(cd "$HERE/../.." && pwd)/build/external}
export PATH=$DEVKITPRO/devkitPPC/bin:$PATH
MACHDEP="-DGEKKO -mrvl -mcpu=750 -meabi -mhard-float"
PCDIRS=$EXTPREFIX/lib/pkgconfig:$DEVKITPRO/portlibs/ppc/lib/pkgconfig
export PKG_CONFIG_PATH=$PCDIRS PKG_CONFIG_LIBDIR=$PCDIRS
[ -f "$EXTPREFIX/lib/libavcodec.a" ] || { echo "build FFmpeg first (make in the top directory)" >&2; exit 1; }

cd "$HERE"
./configure --enable-cross-compile --cc=powerpc-eabi-gcc --host-cc=gcc \
  --target=ppc-linux --enable-big-endian \
  --ar=powerpc-eabi-ar --ranlib=powerpc-eabi-ranlib \
  --nm=powerpc-eabi-nm --as=powerpc-eabi-as --windres=false \
  --disable-ffmpeg_a --enable-static --disable-mencoder --disable-gui \
  --disable-x11 --disable-xv --disable-gl --disable-sdl --disable-alsa --disable-ossaudio \
  --disable-pulse --disable-jack --disable-openal --disable-arts --disable-esd --disable-nas \
  --enable-iconv --disable-dvdread --disable-dvdnav \
  --enable-freetype --disable-fontconfig --enable-ass --enable-ass-internal \
  --enable-networking --enable-ftp --disable-inet6 --disable-unrarexec \
  --disable-termcap --disable-termios --disable-lirc --disable-lircc --disable-joystick \
  --disable-vcd --disable-bluray --disable-cdparanoia --disable-cddb --disable-tv \
  --disable-dvb --disable-pvr --disable-radio --disable-smb --disable-live --disable-nemesi \
  --disable-mad --disable-mp3lame --disable-faad --disable-libvorbis --disable-tremor \
  --disable-mpg123 --disable-libopus --disable-liba52 --disable-libmpeg2 --disable-fribidi \
  --disable-speex --disable-theora --disable-png --disable-jpeg --disable-gif --disable-mng \
  --disable-win32dll --disable-qtx --disable-xanim --disable-real \
  --disable-altivec --disable-vidix --disable-fbdev --disable-runtime-cpudetection \
  --disable-md5sum --disable-yuv4mpeg --disable-pnm --disable-tga \
  --extra-cflags="$MACHDEP -D_GNU_SOURCE -I$DEVKITPRO/libogc/include -I$DEVKITPRO/portlibs/ppc/include -I$EXTPREFIX/include" \
  --extra-ldflags="$MACHDEP -L$DEVKITPRO/libogc/lib/wii -L$DEVKITPRO/portlibs/ppc/lib -L$EXTPREFIX/lib" \
  --extra-libs="-liconv -lbrotlicommon -logc -lm" > configure-wii.log 2>&1 \
  || { tail -20 configure-wii.log; exit 1; }
rm -f config.log configure-wii.log

for f in config.h config.mak; do
  sed -i -e "s#$EXTPREFIX#\$(EXTPREFIX)#g" -e "s#$DEVKITPRO#\$(DEVKITPRO)#g" "$f"
done
# config.h is C, not make: the paths only appear in the configure line
sed -i -e 's#\$(EXTPREFIX)#<EXTPREFIX>#g' -e 's#\$(DEVKITPRO)#<DEVKITPRO>#g' config.h
echo "wrote config.h and config.mak"
