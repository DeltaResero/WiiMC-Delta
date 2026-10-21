# Vendored libraries

devkitPro doesn't package these for the Wii, so WiiMC carries plain copies
(not submodules).

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
