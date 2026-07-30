#include "stdafx.h"
#include "rdb.hpp"

namespace {

    uint32_t ReadBE32(const uint8_t* pData) {
        return (uint32_t(pData[0]) << 24) | (uint32_t(pData[1]) << 16) | (uint32_t(pData[2]) << 8) | uint32_t(pData[3]);
    }

    // AmigaDOS RDB checksum: sum of the given number of big-endian 32-bit
    // words (including the checksum word itself) must wrap around to zero.
    uint32_t ChecksumBlock(const uint8_t* pData, size_t pLongs) {
        uint32_t sum = 0;
        for (size_t i = 0; i < pLongs; i++)
            sum += ReadBE32(pData + i * 4);
        return sum;
    }

    const size_t cRdbAllocationLimit = 16; // AmigaDOS itself only looks this far for "RDSK"
    const size_t cSearchBlockSize = 512;   // RDB search always happens in fixed 512-byte units

    // Offset of PartitionBlock::pb_Environment (the DosEnvec fields), after
    // pb_ID/pb_SummedLongs/pb_ChkSum/pb_HostID/pb_Next/pb_Flags (6*4) +
    // pb_Reserved1[2] (8) + pb_DevFlags (4) + pb_DriveName[32] (32) +
    // pb_Reserved2[15] (60).
    const size_t cPartitionEnvironmentOffset = 6 * 4 + 8 + 4 + 32 + 60;

}

std::vector<sPartitionInfo> ParseRDB(const std::string& pFilePath) {
    std::vector<sPartitionInfo> result;

    std::vector<uint8_t> rdb;
    for (size_t blk = 0; blk < cRdbAllocationLimit; blk++) {
        auto data = firy::gResources->FileRead(pFilePath, blk * cSearchBlockSize, cSearchBlockSize);
        if (!data || data->size() < cSearchBlockSize)
            return result;

        if (memcmp(data->data(), "RDSK", 4) == 0) {
            rdb.assign(data->data(), data->data() + data->size());
            break;
        }
    }

    if (rdb.empty())
        return result;

    uint32_t summedLongs = ReadBE32(rdb.data() + 4) & 0x7f; // rdb_SummedLongs
    if ((size_t)summedLongs * 4 > rdb.size() || ChecksumBlock(rdb.data(), summedLongs) != 0)
        return result;

    uint32_t blockBytes = ReadBE32(rdb.data() + 16); // rdb_BlockBytes
    if (blockBytes < 128 || blockBytes >(uint32_t)(16 * 1024 * 1024))
        return result; // sanity check against a corrupt/garbage block that happened to start with "RDSK"

    int32_t partitionBlock = (int32_t)ReadBE32(rdb.data() + 28); // rdb_PartitionList

    for (int part = 0; partitionBlock > 0 && part < 16; part++) {
        auto data = firy::gResources->FileRead(pFilePath, (size_t)partitionBlock * blockBytes, blockBytes);
        if (!data || data->size() < blockBytes)
            break;

        const uint8_t* pb = data->data();

        if (memcmp(pb, "PART", 4) != 0)
            break;

        uint32_t pbSummedLongs = ReadBE32(pb + 4) & 0x7f; // pb_SummedLongs
        if ((size_t)pbSummedLongs * 4 > blockBytes || ChecksumBlock(pb, pbSummedLongs) != 0)
            break;

        int32_t next = (int32_t)ReadBE32(pb + 16); // pb_Next

        uint8_t nameLen = pb[36]; // pb_DriveName is a 1-byte-length-prefixed BSTR
        std::string name((const char*)pb + 37, (std::min)((size_t)nameLen, (size_t)31));

        const uint8_t* env = pb + cPartitionEnvironmentOffset;
        uint32_t surfaces = ReadBE32(env + 3 * 4);       // de_Surfaces
        uint32_t blocksPerTrack = ReadBE32(env + 5 * 4); // de_BlocksPerTrack
        uint32_t lowCyl = ReadBE32(env + 9 * 4);         // de_LowCyl
        uint32_t highCyl = ReadBE32(env + 10 * 4);       // de_HighCyl
        uint32_t dosType = ReadBE32(env + 16 * 4);       // de_DosType

        uint64_t cylBlocks = (uint64_t)surfaces * blocksPerTrack;
        uint64_t startBlock = (uint64_t)lowCyl * cylBlocks;
        uint64_t blockCount = highCyl >= lowCyl ? ((uint64_t)highCyl - lowCyl + 1) * cylBlocks : 0;

        // Only AmigaDOS filesystems ("DOS\0".."DOS\7", international/
        // dircache variants) - firy's cADF can't read PFS/SFS/other
        // third-party filesystems anyway.
        if ((dosType & 0xffffff00) == 0x444f5300 && blockCount > 0) {
            sPartitionInfo info;
            info.mName = name.empty() ? ("Partition" + std::to_string(result.size() + 1)) : name;
            info.mOffset = (size_t)(startBlock * blockBytes);
            info.mSize = (size_t)(blockCount * blockBytes);
            result.push_back(info);
        }

        partitionBlock = next;
    }

    return result;
}
