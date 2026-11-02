# Vendored libraries

devkitPro doesn't package these for the Wii, so WiiMC carries copies of
them here, with the local changes each section lists.
The top-level Makefile builds libntfs and libiconv into `build/external`
(headers in `include/`, static libraries in `lib/`) before MPlayer and
WiiMC, and never installs anything into `$DEVKITPRO`. `make clean` removes
them.

## libntfs

- Upstream: https://github.com/extremscorner/libntfs
- Version: master, commit `f73a4d1ff9e2d6bd8fdc7c8caa3a74df74803103`
  ("Updated to NTFS-3G 2026.9.28", 2026-09-29)
- Fetched: 2026-10-09 (`git archive` of that commit, no `.git`)
- Licence: GPL version 2 or later (`LICENSE`)

Local patch, so it builds against devkitPro's libogc 3 instead of libogc2:

- `DISC_INTERFACE` hooks are called in devkitPro's form, without the
  interface pointer as first argument.
- Interface pointers are `const DISC_INTERFACE *`, because devkitPro
  declares `__io_wiisd` and `__io_wiidvd` const. This changes the
  prototypes of `ntfsMount`, `ntfsMountDevice` and `ntfsFindPartitions`.
- devkitPro's `DISC_INTERFACE` has no `flush` hook, so `_NTFS_cache_flush`
  and `ntfs_device_gekko_io_sync` stop after writing back dirty pages.
- `source/cache2.c` carries a copy of libogc2's `SYS_IsDMAAddress`, which
  libogc 3 lacks.
- `source/types.h` includes `<gcbool.h>` for `BOOL`, `TRUE` and `FALSE`.
- `source/ea.c` maps `makedev` to libogc 3's `mkdev`.

## libiconv

- Upstream: https://ftp.gnu.org/gnu/libiconv/
- Version: GNU libiconv 1.19 (`libiconv-1.19.tar.gz`, signature checked
  against the GNU keyring, signed by Bruno Haible)
- SHA-256 of the tarball:
  `88dd96a8c0464eca144fc791ae60cd31cd8ee78321e67397e25fc095c4a19aa6`
- Fetched: 2026-10-09
- Licence: the library (`lib/`, `libcharset/`, `include/`) is LGPL 2.1 or
  later (`COPYING.LIB`); the `iconv` program is GPL 3 or later (`COPYING`).
- Trimmed to the library: the `iconv` program and its gnulib copy
  (`src/`, `srclib/`), tests, translations, man pages, Windows and OS/2
  files and the gnulib import tooling are removed. `configure` still reads
  the `Makefile.in` of each removed directory, so those stay.

Local patch, which keeps about 650 KB of tables out of MEM1:

- `lib/encodings.def` leaves out the converters of the CJK encodings that
  nothing in WiiMC can ask for: EUC-JP, CP932, ISO-2022-JP (and -1, -2,
  -MS), GBK, CP936, GB18030, ISO-2022-CN (and -EXT), HZ, EUC-TW, CP950,
  BIG5-HKSCS, JOHAB, ISO-2022-KR, and the JIS X 0212 and ISO-IR-165
  character sets. The subtitle codepages in WiiMC's settings (Shift_JIS,
  GB2312, Big5 and CP949 among them) and EUC-KR keep theirs.
- `lib/iconv_open1.h` rejects an encoding without converters, so
  `iconv_open` fails with `EINVAL` as it does for an unknown name.
- `lib/loop_unicode.h` no longer transliterates (`//TRANSLIT`). WiiMC
  converts only to UTF-8 and UTF-16, which never need it.

## projectM

- Upstream: https://github.com/projectM-visualizer/projectm
- Version: libprojectM from 2007-2009, ported to the Wii by Rhys "Shareese"
  Koedijk in 2009; in WiiMC since its first commit
- Licence: LGPL 2.1 or later (`LICENSE`)
- A music visualiser that plays MilkDrop presets.

## FriBidi

- Upstream: https://github.com/fribidi/fribidi
- Version: GNU FriBidi 0.19.2; in WiiMC since its first commit
- Licence: LGPL 2.1 or later (`COPYING`)
- Lays out right-to-left text, such as Arabic and Hebrew.

## FFmpeg

- Upstream: https://git.ffmpeg.org/ffmpeg.git
- Version: FFmpeg 9.0.2 (tag `n9.0.2`, commit
  `946fcce07b6dcd0331c8cc609192aeff5e1924f8`)
- Fetched: 2026-11-03
- Licence: LGPL 2.1 or later (`COPYING.LGPLv2.1`, `LICENSE.md`); WiiMC
  builds it with the GPL parts enabled, which makes the result GPL.
- Trimmed to what WiiMC's build reads: the sources and headers its objects
  are compiled from, the build machinery (`configure`, `ffbuild/`, the
  library Makefiles and version scripts), the component lists `configure`
  scans, the libraries' public headers, and the licence and release files.
  1,689 of the 10,424 files remain. A build of the trimmed tree compiles
  to the same code as one of the whole tree.

Local changes:

- `Makefile` includes the Makefiles of `tools/`, `fftools/`, `doc/` and
  `tests/` only if they exist, since the trim removes those directories.
- `libavutil/tx_template.c`: with `FF_TX_SMALL_SR_TABLES` the static
  split-radix tables and their codelets stop at 2^15 points instead of
  2^17. Longer power-of-two transforms use the generic codelets.
- `libavcodec/h264*.c`: with `FF_H264_8BIT_ONLY` the H.264 DSP,
  prediction, IDCT and qpel functions are built for 8-bit video only, and
  the SPS parser turns away a stream of any other bit depth.
- `libavutil/tx.c`: with `FF_TX_FLOAT_ONLY` only the float transforms are
  listed, so the double and int32 ones aren't linked, and `av_tx_init()`
  returns `AVERROR(ENOSYS)` for those types.
- `configure`: the PowerPC probe for `dcbzl` only runs when the option is
  still on, so `--disable-dcbzl` takes effect.
