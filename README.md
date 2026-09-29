# diropus-amiga-c64-disc-image-plugin

**Status: v0.1.0 (beta 4)**

VFS plugin for Directory Opus that browses disk images directly in the
Lister: Amiga (ADF/FDI/HDF/DMS), Commodore 64 (D64/D71/D81), FAT12/16/32 and
ISO9660 - whatever [firy](https://github.com/segrax/firy) supports, since it
does the actual image parsing.

## Features

* Browse, read, delete and import files/folders inside disk images, same as
  a real folder in the Lister
* Amiga: `.adf` / `.fdi` / `.hdf` (including RDB-partitioned hard disk
  images, multiple partitions shown as folders) and `.dms` archives
* Commodore 64: `.d64` / `.d71` / `.d81`
* Bonus, courtesy of firy: FAT12/16/32 (`.img`/`.ima`) and ISO9660 (`.iso`)
* New disk formats firy adds show up automatically - the extension list and
  image detection come straight from firy, not a hardcoded list here

## How it works

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
with firy.

## Requirements

* Directory Opus for Windows
* [firy](https://github.com/segrax/firy) checked out as a sibling folder
  (`../firy`) - built directly into this plugin, not vendored in this repo
* Visual Studio 2019 or newer (v142+ toolset), C++17

## Building

```
git clone https://github.com/gh0stless/directory-opus-amiga-c64-disc-image-plugin.git
git clone https://github.com/segrax/firy.git
git -C firy apply ../directory-opus-amiga-c64-disc-image-plugin/patches/firy-d64-accept-any-dos-type.patch
```

The patch in `patches/` fixes a firy bug that refused to open D64/D71/D81
images whose disk DOS type isn't the stock `2A`/`3D`; release builds include it.

With both checked out next to each other, open `opusADF.sln` and build
`Release|x64`. Output is `x64\Release\opusAmigaDisc.dll`.

## Installing

Copy `opusAmigaDisc.dll` into Directory Opus's `VFSPlugins` folder (usually
`C:\Program Files\GPSoftware\Directory Opus\VFSPlugins`), then restart Opus
or refresh plugins.

## Known limitations (beta)

* Encrypted DMS archives aren't supported
* No UI for creating a partitioned .hdf or an RDB from scratch (read/import/
  delete into existing partitions works)
* Rename and SetAttr aren't implemented
