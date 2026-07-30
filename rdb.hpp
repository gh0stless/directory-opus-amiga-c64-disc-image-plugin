#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * Amiga RigidDiskBlock (RDB) partition table parsing.
 *
 * Real .hdf hard-disk images almost always start with an RDB rather than
 * a plain AmigaDOS boot block (unlike floppy .adf images). firy's cADF
 * expects a boot block at byte 0 and has no idea RDB exists, so we parse
 * the partition table ourselves and hand firy just the byte range of one
 * partition at a time (via cOffsetSource) - same idea as the DMS
 * unpacking, just slicing instead of decompressing.
 *
 * Struct layout is the standard AmigaDOS RigidDiskBlock/PartitionBlock/
 * DosEnvec format, cross-checked field-for-field against Linux's
 * block/partitions/amiga.c, which parses the same on-disk structures.
 */
struct sPartitionInfo {
    std::string mName;
    size_t mOffset; // byte offset of this partition within the .hdf file
    size_t mSize;   // byte length of this partition
};

// Returns the AmigaDOS ("DOS\x") partitions described by the RDB at the
// start of pFilePath, or an empty vector if there's no valid RDB there.
std::vector<sPartitionInfo> ParseRDB(const std::string& pFilePath);
