# Directory Opus Amiga/C64 Disk Image Plugin

VFS plugin for Directory Opus that browses disk images directly in the
Lister: Amiga (ADF/FDI/HDF/DMS), Commodore 64 (D64/D71/D81), FAT12/16/32 and
ISO9660 - whatever [firy](https://github.com/segrax/firy) supports, since it
does the actual image parsing.

DMS (DiskMasher) archives are unpacked to an in-memory ADF and handed to
firy as a `cMemorySource` - firy itself has no idea DMS exists. The decoder
(`dms.hpp`/`dms.cpp`) is a from-scratch C++ port of
[xDMS](https://gitlab.com/heikkiorsila/xdms) (Public Domain), covering all
compression modes (RLE/Quick/Medium/Deep/Heavy1/Heavy2). Encrypted archives
aren't supported (no password prompt in this VFS plugin).

Real .hdf hard-disk images almost always carry an RDB (Rigid Disk Block)
partition table rather than a plain AmigaDOS boot block at byte 0 - firy's
`cADF` doesn't know RDB exists either, so `rdb.hpp`/`rdb.cpp` parses it
ourselves (RigidDiskBlock/PartitionBlock/DosEnvec, checksummed the same way
AmigaDOS does) and every AmigaDOS partition found shows up as a folder at
the .hdf's root. Picking a partition hands firy a `cOffsetSource`
(`offsetsource.hpp`) - a byte-range window into the real .hdf file at that
partition's offset - so a multi-gigabyte hard disk image is never fully
loaded into memory, unlike the DMS path. Non-AmigaDOS partitions (PFS, SFS,
...) are skipped; firy couldn't read them anyway.

Fork of [segrax/directory_opus_amiga_plugin](https://github.com/segrax/directory_opus_amiga_plugin)
(ADFlib-based, Amiga-only, unmaintained since 2019). ADFlib has been replaced
with firy; extension list and image detection now come from firy directly, so
new formats firy adds show up here without code changes.

Supports directory listing, reading, deleting, and importing files/folders,
mirroring the feature set of the original ADFlib-based plugin.

#### Requirements

* Directory Opus for Windows
* [firy](https://github.com/segrax/firy) checked out as a sibling folder
  (`../firy`) - built directly into this plugin, not vendored in this repo
