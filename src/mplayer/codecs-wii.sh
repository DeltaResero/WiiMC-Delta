#!/bin/sh
# Regenerate codecs.conf.h, MPlayer's built-in codec table, for WiiMC.
#
# The table keeps only entries whose driver is compiled into WiiMC; the
# others (Windows, QuickTime and RealPlayer binary loaders, external codec
# libraries) can never load on the Wii. Every "ffmpeg" entry stays, also
# for decoders that src/lib/ffmpeg-wii.mak leaves switched off, so turning
# one on needs no table change, except the float MPEG audio decoders: they
# come before the integer ones and aren't built, so every MP1, MP2 or MP3
# file would log a failed open first. Run from
# src/mplayer on the build host after the top-level Makefile has built
# FFmpeg (codec-cfg needs its headers):
#
#   EXTPREFIX=<repo>/build/external ./codecs-wii.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
EXTPREFIX=${EXTPREFIX:-$(cd "$HERE/../.." && pwd)/build/external}
HOST_CC=${HOST_CC:-gcc}
# libmpcodecs/ad_*.c and vd_*.c that WiiMC's config.mak builds
DRIVERS="alaw dvdpcm ffmpeg hwac3 hwmpa pcm spdif black hmblck lzo mpegpes mtga null raw sgi"
# entries left out by name
SKIP="ffmp3on4float ffmp3float ffmp3adufloat ffmp2float"

cd "$HERE"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# An entry runs from its videocodec/audiocodec line to the next one; the
# comments before the first entry stay as they are.
awk -v keep="$DRIVERS" -v skip="$SKIP" '
BEGIN { n = split(keep, k, " "); for (i = 1; i <= n; i++) ok[k[i]] = 1
        n = split(skip, k, " "); for (i = 1; i <= n; i++) no[k[i]] = 1 }
function flush() { if (buf != "" && (drv in ok) && !(name in no)) printf "%s", buf; buf = ""; drv = "" }
/^(videocodec|audiocodec)[ \t]/ { flush(); inentry = 1; name = $2 }
!inentry { print; next }
{ buf = buf $0 "\n" }
/^[ \t]*driver[ \t]/ { drv = $2 }
END { flush() }
' etc/codecs.conf > "$TMP/codecs.conf"

[ -f help_mp.h ] || help/help_create.sh help/help_mp-en.h UTF-8
$HOST_CC -O -DCODECS2HTML -I. -I"$EXTPREFIX/include" -o "$TMP/codec-cfg" codec-cfg.c
"$TMP/codec-cfg" "$TMP/codecs.conf" |
  sed '1s#.*#/* GENERATED FROM etc/codecs.conf BY codecs-wii.sh, DO NOT EDIT! */#' > codecs.conf.h
echo "codecs.conf.h: $(grep -c '/\* drv \*/' codecs.conf.h) entries"
