#pragma once

#include <cstdint>
#include <vector>

/**
 * DMS (DiskMasher) decompressor.
 *
 * Ported from xDMS v1.3 (Public Domain, Andre Rodrigues de la Rocha,
 * https://gitlab.com/heikkiorsila/xdms) so a .dms archive can be unpacked
 * to a plain ADF byte buffer in memory and handed to firy as a
 * cMemorySource, without touching firy itself or the disk.
 *
 * Only the parts needed to read a disk image are ported: extracting the
 * per-track data (modes NONE/RLE/QUICK/MEDIUM/DEEP/HEAVY1/HEAVY2).
 * Encrypted archives, the banner and FILEID.DIZ are not handled.
 */
class cDmsUnpacker {

public:
    // Returns the decompressed ADF-layout bytes, or an empty vector if
    // pInput isn't a (supported) DMS archive.
    std::vector<uint8_t> Unpack(const std::vector<uint8_t>& pInput);

private:
    // ---- bit reader (MSB-first, matches xdms getbits.h) ----
    const uint8_t* mInData = nullptr;
    uint32_t mBitBuf = 0;
    int mBitCount = 0;
    void InitBitBuf(const uint8_t* pIn);
    uint16_t GetBits(int pBits) const;
    void DropBits(int pBits);

    // ---- shared sliding-window dictionary, reused across modes/tracks ----
    static const size_t cTextBufferLen = 32000;
    std::vector<uint8_t> mText;

    uint16_t mQuickTextLoc = 251;
    uint16_t mMediumTextLoc = 0x3fbe;
    uint16_t mDeepTextLoc = 0x3fc4;
    uint16_t mHeavyTextLoc = 0;
    uint16_t mHeavyLastLen = 0;

    void InitDecrunchers();

    // ---- mode 1: RLE ----
    bool UnpackRLE(const uint8_t* pIn, uint8_t* pOut, uint16_t pOrigSize);

    // ---- mode 2: QUICK ----
    bool UnpackQuick(const uint8_t* pIn, uint8_t* pOut, uint16_t pOrigSize);

    // ---- mode 3: MEDIUM (LZSS with a fixed table) ----
    bool UnpackMedium(const uint8_t* pIn, uint8_t* pOut, uint16_t pOrigSize);

    // ---- mode 4: DEEP (LZSS with an adaptive Huffman tree, from LZHUF) ----
    bool mInitDeepTabs = true;
    static const int cDeepF = 60;
    static const int cDeepThreshold = 2;
    static const int cDeepNChar = 256 - cDeepThreshold + cDeepF;
    static const int cDeepT = cDeepNChar * 2 - 1;
    static const int cDeepR = cDeepT - 1;
    static const int cDeepMaxFreq = 0x8000;

    std::vector<uint16_t> mDeepFreq;
    std::vector<uint16_t> mDeepPrnt;
    std::vector<uint16_t> mDeepSon;

    void InitDeepTabs();
    bool UnpackDeep(const uint8_t* pIn, uint8_t* pOut, uint16_t pOrigSize);
    uint16_t DeepDecodeChar();
    uint16_t DeepDecodePosition();
    void DeepUpdate(uint16_t pCode);
    void DeepReconst();

    // ---- modes 5/6: HEAVY1/HEAVY2 (canonical Huffman LZH, from lha) ----
    static const int cHeavyNC = 510;
    static const int cHeavyNPT = 20;

    std::vector<uint16_t> mHeavyLeft, mHeavyRight;
    uint8_t mHeavyCLen[cHeavyNC] = {};
    uint8_t mHeavyPtLen[cHeavyNPT] = {};
    uint16_t mHeavyCTable[4096] = {};
    uint16_t mHeavyPtTable[256] = {};
    uint16_t mHeavyNp = 0;

    bool UnpackHeavy(const uint8_t* pIn, uint8_t* pOut, uint8_t pFlags, uint16_t pOrigSize);
    uint16_t HeavyDecodeC();
    uint16_t HeavyDecodeP();
    bool HeavyReadTreeC();
    bool HeavyReadTreeP();
    int MakeTable(int pNChar, const uint8_t* pBitLen, int pTableBits, uint16_t* pTable);

    // recursive helper state for MakeTable (mirrors maketbl.c's statics)
    struct sMakeTableState {
        int c;
        int n, tblsiz, len, depth, maxdepth, avail;
        uint16_t codeword, bit;
        uint16_t* tbl;
        const uint8_t* blen;
        int err;
    };
    uint16_t MakeTableRecurse(sMakeTableState& pState);

    bool UnpackTrack(const uint8_t* pIn, uint8_t* pOut, uint16_t pPkLen2, uint16_t pUnpkLen, uint8_t pCMode, uint8_t pFlags);

    static uint16_t CreateCRC(const uint8_t* pMem, size_t pSize);
    static uint16_t CalcCheckSum(const uint8_t* pMem, size_t pSize);
};

std::vector<uint8_t> DecompressDMS(const std::vector<uint8_t>& pInput);
