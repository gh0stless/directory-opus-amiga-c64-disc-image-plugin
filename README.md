# Directory Opus Amiga/C64 Disk Image Plugin

VFS plugin for Directory Opus that browses disk images directly in the
Lister: Amiga (ADF/FDI/HDF), Commodore 64 (D64/D71/D81), FAT12/16/32 and
ISO9660 - whatever [firy](https://github.com/segrax/firy) supports, since it
does the actual image parsing.

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
