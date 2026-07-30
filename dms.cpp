/*
 * DMS (DiskMasher) decompressor.
 *
 * Ported from xDMS v1.3 - Portable DMS archive unpacker - Public Domain
 * Written by Andre Rodrigues de la Rocha <adlroc@usa.net>
 * https://gitlab.com/heikkiorsila/xdms
 *
 * The Heavy1/Heavy2 Huffman table builder and decoder are adapted from
 * LHA (Masaru Oki); the Deep adaptive-Huffman coder is adapted from
 * LZHUF (Haruyasu Yoshizaki) - same lineage as in the original xDMS.
 *
 * Only what's needed to read a disk image out of a .dms archive is
 * ported here: per-track extraction for modes NONE/RLE/QUICK/MEDIUM/
 * DEEP/HEAVY1/HEAVY2. Encrypted archives, the banner and FILEID.DIZ
 * are intentionally not handled.
 */

#include "stdafx.h"
#include "dms.hpp"
#include <algorithm>
#include <cstring>

namespace {

    const uint32_t cMaskBits[25] = {
        0x000000, 0x000001, 0x000003, 0x000007, 0x00000f, 0x00001f,
        0x00003f, 0x00007f, 0x0000ff, 0x0001ff, 0x0003ff, 0x0007ff,
        0x000fff, 0x001fff, 0x003fff, 0x007fff, 0x00ffff, 0x01ffff,
        0x03ffff, 0x07ffff, 0x0fffff, 0x1fffff, 0x3fffff, 0x7fffff,
        0xffffff
    };

    // Shared by Medium (distance high/low bytes) and Deep (distance byte)
    const uint8_t cDCode[256] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
        0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
        0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
        0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
        0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
        0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
        0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
        0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
        0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
        0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
        0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
        0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
        0x0C, 0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D,
        0x0E, 0x0E, 0x0E, 0x0E, 0x0F, 0x0F, 0x0F, 0x0F,
        0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11, 0x11,
        0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13,
        0x14, 0x14, 0x14, 0x14, 0x15, 0x15, 0x15, 0x15,
        0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17, 0x17,
        0x18, 0x18, 0x19, 0x19, 0x1A, 0x1A, 0x1B, 0x1B,
        0x1C, 0x1C, 0x1D, 0x1D, 0x1E, 0x1E, 0x1F, 0x1F,
        0x20, 0x20, 0x21, 0x21, 0x22, 0x22, 0x23, 0x23,
        0x24, 0x24, 0x25, 0x25, 0x26, 0x26, 0x27, 0x27,
        0x28, 0x28, 0x29, 0x29, 0x2A, 0x2A, 0x2B, 0x2B,
        0x2C, 0x2C, 0x2D, 0x2D, 0x2E, 0x2E, 0x2F, 0x2F,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    };

    const uint8_t cDLen[256] = {
        0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
        0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
        0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
        0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
        0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
        0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
        0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
        0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
        0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
        0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
        0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
        0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
        0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
        0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
        0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
        0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
        0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
        0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
        0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
        0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
        0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
        0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
        0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
        0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
        0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
        0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
        0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
        0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
        0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
        0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
        0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
        0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    };

    const uint16_t cCRCTab[256] = {
        0x0000,0xC0C1,0xC181,0x0140,0xC301,0x03C0,0x0280,0xC241,
        0xC601,0x06C0,0x0780,0xC741,0x0500,0xC5C1,0xC481,0x0440,
        0xCC01,0x0CC0,0x0D80,0xCD41,0x0F00,0xCFC1,0xCE81,0x0E40,
        0x0A00,0xCAC1,0xCB81,0x0B40,0xC901,0x09C0,0x0880,0xC841,
        0xD801,0x18C0,0x1980,0xD941,0x1B00,0xDBC1,0xDA81,0x1A40,
        0x1E00,0xDEC1,0xDF81,0x1F40,0xDD01,0x1DC0,0x1C80,0xDC41,
        0x1400,0xD4C1,0xD581,0x1540,0xD701,0x17C0,0x1680,0xD641,
        0xD201,0x12C0,0x1380,0xD341,0x1100,0xD1C1,0xD081,0x1040,
        0xF001,0x30C0,0x3180,0xF141,0x3300,0xF3C1,0xF281,0x3240,
        0x3600,0xF6C1,0xF781,0x3740,0xF501,0x35C0,0x3480,0xF441,
        0x3C00,0xFCC1,0xFD81,0x3D40,0xFF01,0x3FC0,0x3E80,0xFE41,
        0xFA01,0x3AC0,0x3B80,0xFB41,0x3900,0xF9C1,0xF881,0x3840,
        0x2800,0xE8C1,0xE981,0x2940,0xEB01,0x2BC0,0x2A80,0xEA41,
        0xEE01,0x2EC0,0x2F80,0xEF41,0x2D00,0xEDC1,0xEC81,0x2C40,
        0xE401,0x24C0,0x2580,0xE541,0x2700,0xE7C1,0xE681,0x2640,
        0x2200,0xE2C1,0xE381,0x2340,0xE101,0x21C0,0x2080,0xE041,
        0xA001,0x60C0,0x6180,0xA141,0x6300,0xA3C1,0xA281,0x6240,
        0x6600,0xA6C1,0xA781,0x6740,0xA501,0x65C0,0x6480,0xA441,
        0x6C00,0xACC1,0xAD81,0x6D40,0xAF01,0x6FC0,0x6E80,0xAE41,
        0xAA01,0x6AC0,0x6B80,0xAB41,0x6900,0xA9C1,0xA881,0x6840,
        0x7800,0xB8C1,0xB981,0x7940,0xBB01,0x7BC0,0x7A80,0xBA41,
        0xBE01,0x7EC0,0x7F80,0xBF41,0x7D00,0xBDC1,0xBC81,0x7C40,
        0xB401,0x74C0,0x7580,0xB541,0x7700,0xB7C1,0xB681,0x7640,
        0x7200,0xB2C1,0xB381,0x7340,0xB101,0x71C0,0x7080,0xB041,
        0x5000,0x90C1,0x9181,0x5140,0x9301,0x53C0,0x5280,0x9241,
        0x9601,0x56C0,0x5780,0x9741,0x5500,0x95C1,0x9481,0x5440,
        0x9C01,0x5CC0,0x5D80,0x9D41,0x5F00,0x9FC1,0x9E81,0x5E40,
        0x5A00,0x9AC1,0x9B81,0x5B40,0x9901,0x59C0,0x5880,0x9841,
        0x8801,0x48C0,0x4980,0x8941,0x4B00,0x8BC1,0x8A81,0x4A40,
        0x4E00,0x8EC1,0x8F81,0x4F40,0x8D01,0x4DC0,0x4C80,0x8C41,
        0x4400,0x84C1,0x8581,0x4540,0x8701,0x47C0,0x4680,0x8641,
        0x8201,0x42C0,0x4380,0x8341,0x4100,0x81C1,0x8081,0x4040
    };

    const size_t cHeaderLen = 56;
    const size_t cTrackHeaderLen = 20;

}

uint16_t cDmsUnpacker::CreateCRC(const uint8_t* pMem, size_t pSize) {
    uint16_t crc = 0;
    while (pSize--)
        crc = (uint16_t)(cCRCTab[(crc ^ *pMem++) & 255] ^ ((crc >> 8) & 255));
    return crc;
}

uint16_t cDmsUnpacker::CalcCheckSum(const uint8_t* pMem, size_t pSize) {
    uint16_t sum = 0;
    while (pSize--)
        sum = (uint16_t)(sum + *pMem++);
    return sum;
}

void cDmsUnpacker::InitBitBuf(const uint8_t* pIn) {
    mBitBuf = 0;
    mBitCount = 0;
    mInData = pIn;
    DropBits(0);
}

uint16_t cDmsUnpacker::GetBits(int pBits) const {
    return (uint16_t)(mBitBuf >> (mBitCount - pBits));
}

void cDmsUnpacker::DropBits(int pBits) {
    mBitCount -= pBits;
    mBitBuf &= cMaskBits[mBitCount];
    while (mBitCount < 16) {
        mBitBuf = (mBitBuf << 8) | *mInData++;
        mBitCount += 8;
    }
}

void cDmsUnpacker::InitDecrunchers() {
    mQuickTextLoc = 251;
    mMediumTextLoc = 0x3fbe;
    mHeavyLastLen = 0;
    mHeavyTextLoc = 0;
    mDeepTextLoc = 0x3fc4;
    mInitDeepTabs = true;
    std::fill(mText.begin(), mText.begin() + 0x3fc8, (uint8_t)0);
}

// ---- mode 1: RLE ----

bool cDmsUnpacker::UnpackRLE(const uint8_t* pIn, uint8_t* pOut, uint16_t pOrigSize) {
    uint8_t* outEnd = pOut + pOrigSize;

    while (pOut < outEnd) {
        uint8_t a = *pIn++;
        if (a != 0x90) {
            *pOut++ = a;
            continue;
        }

        uint8_t b = *pIn++;
        if (!b) {
            *pOut++ = a;
            continue;
        }

        a = *pIn++;
        uint16_t n;
        if (b == 0xff) {
            n = *pIn++;
            n = (uint16_t)((n << 8) + *pIn++);
        }
        else {
            n = b;
        }

        if (pOut + n > outEnd)
            return false;

        memset(pOut, a, n);
        pOut += n;
    }

    return true;
}

// ---- mode 2: QUICK ----

bool cDmsUnpacker::UnpackQuick(const uint8_t* pIn, uint8_t* pOut, uint16_t pOrigSize) {
    const uint16_t bitmask = 0xff;

    InitBitBuf(pIn);

    uint8_t* outEnd = pOut + pOrigSize;
    while (pOut < outEnd) {
        if (GetBits(1) != 0) {
            DropBits(1);
            uint8_t ch = (uint8_t)GetBits(8); DropBits(8);
            mText[mQuickTextLoc++ & bitmask] = ch;
            *pOut++ = ch;
        }
        else {
            DropBits(1);
            uint16_t j = (uint16_t)(GetBits(2) + 2); DropBits(2);
            uint16_t i = (uint16_t)(mQuickTextLoc - GetBits(8) - 1); DropBits(8);
            while (j--) {
                uint8_t ch = mText[i++ & bitmask];
                mText[mQuickTextLoc++ & bitmask] = ch;
                *pOut++ = ch;
            }
        }
    }
    mQuickTextLoc = (uint16_t)((mQuickTextLoc + 5) & bitmask);

    return true;
}

// ---- mode 3: MEDIUM ----

bool cDmsUnpacker::UnpackMedium(const uint8_t* pIn, uint8_t* pOut, uint16_t pOrigSize) {
    const uint16_t bitmask = 0x3fff;

    InitBitBuf(pIn);

    uint8_t* outEnd = pOut + pOrigSize;
    while (pOut < outEnd) {
        if (GetBits(1) != 0) {
            DropBits(1);
            uint8_t ch = (uint8_t)GetBits(8); DropBits(8);
            mText[mMediumTextLoc++ & bitmask] = ch;
            *pOut++ = ch;
        }
        else {
            DropBits(1);
            uint16_t c = GetBits(8); DropBits(8);
            uint16_t j = (uint16_t)(cDCode[c] + 3);
            uint8_t u = cDLen[c];
            c = (uint16_t)(((c << u) | GetBits(u)) & 0xff); DropBits(u);
            u = cDLen[c];
            c = (uint16_t)((cDCode[c] << 8) | (((c << u) | GetBits(u)) & 0xff)); DropBits(u);
            uint16_t i = (uint16_t)(mMediumTextLoc - c - 1);

            while (j--) {
                uint8_t ch = mText[i++ & bitmask];
                mText[mMediumTextLoc++ & bitmask] = ch;
                *pOut++ = ch;
            }
        }
    }
    mMediumTextLoc = (uint16_t)((mMediumTextLoc + 66) & bitmask);

    return true;
}

// ---- mode 4: DEEP ----

void cDmsUnpacker::InitDeepTabs() {
    for (int i = 0; i < cDeepNChar; i++) {
        mDeepFreq[i] = 1;
        mDeepSon[i] = (uint16_t)(i + cDeepT);
        mDeepPrnt[i + cDeepT] = (uint16_t)i;
    }

    int i = 0, j = cDeepNChar;
    while (j <= cDeepR) {
        mDeepFreq[j] = (uint16_t)(mDeepFreq[i] + mDeepFreq[i + 1]);
        mDeepSon[j] = (uint16_t)i;
        mDeepPrnt[i] = mDeepPrnt[i + 1] = (uint16_t)j;
        i += 2; j++;
    }
    mDeepFreq[cDeepT] = 0xffff;
    mDeepPrnt[cDeepR] = 0;

    mInitDeepTabs = false;
}

void cDmsUnpacker::DeepReconst() {
    uint16_t j = 0;
    for (uint16_t i = 0; i < cDeepT; i++) {
        if (mDeepSon[i] >= cDeepT) {
            mDeepFreq[j] = (uint16_t)((mDeepFreq[i] + 1) / 2);
            mDeepSon[j] = mDeepSon[i];
            j++;
        }
    }

    for (uint16_t i = 0, jj = cDeepNChar; jj < cDeepT; i += 2, jj++) {
        uint16_t k = (uint16_t)(i + 1);
        uint16_t f = mDeepFreq[jj] = (uint16_t)(mDeepFreq[i] + mDeepFreq[k]);
        for (k = (uint16_t)(jj - 1); f < mDeepFreq[k]; k--);
        k++;
        uint16_t l = (uint16_t)((jj - k) * 2);
        memmove(&mDeepFreq[k + 1], &mDeepFreq[k], l * sizeof(uint16_t));
        mDeepFreq[k] = f;
        memmove(&mDeepSon[k + 1], &mDeepSon[k], l * sizeof(uint16_t));
        mDeepSon[k] = i;
    }

    for (uint16_t i = 0; i < cDeepT; i++) {
        uint16_t k = mDeepSon[i];
        if (k >= cDeepT) {
            mDeepPrnt[k] = i;
        }
        else {
            mDeepPrnt[k] = mDeepPrnt[k + 1] = i;
        }
    }
}

void cDmsUnpacker::DeepUpdate(uint16_t pCode) {
    if (mDeepFreq[cDeepR] == cDeepMaxFreq) {
        DeepReconst();
    }

    uint16_t c = mDeepPrnt[pCode + cDeepT];
    do {
        uint16_t k = ++mDeepFreq[c];
        uint16_t l = (uint16_t)(c + 1);

        if (k > mDeepFreq[l]) {
            while (k > mDeepFreq[++l]);
            l--;
            mDeepFreq[c] = mDeepFreq[l];
            mDeepFreq[l] = k;

            uint16_t i = mDeepSon[c];
            mDeepPrnt[i] = l;
            if (i < cDeepT) mDeepPrnt[i + 1] = l;

            uint16_t j = mDeepSon[l];
            mDeepSon[l] = i;

            mDeepPrnt[j] = c;
            if (j < cDeepT) mDeepPrnt[j + 1] = c;
            mDeepSon[c] = j;

            c = l;
        }
    } while ((c = mDeepPrnt[c]) != 0);
}

uint16_t cDmsUnpacker::DeepDecodeChar() {
    uint16_t c = mDeepSon[cDeepR];

    while (c < cDeepT) {
        c = mDeepSon[c + GetBits(1)];
        DropBits(1);
    }
    c -= cDeepT;
    DeepUpdate(c);
    return c;
}

uint16_t cDmsUnpacker::DeepDecodePosition() {
    uint16_t i = GetBits(8); DropBits(8);
    uint16_t c = (uint16_t)(cDCode[i] << 8);
    uint16_t j = cDLen[i];
    i = (uint16_t)(((i << j) | GetBits(j)) & 0xff); DropBits(j);
    return (uint16_t)(c | i);
}

bool cDmsUnpacker::UnpackDeep(const uint8_t* pIn, uint8_t* pOut, uint16_t pOrigSize) {
    const uint16_t bitmask = 0x3fff;

    InitBitBuf(pIn);

    if (mInitDeepTabs)
        InitDeepTabs();

    uint8_t* outEnd = pOut + pOrigSize;
    while (pOut < outEnd) {
        uint16_t c = DeepDecodeChar();
        if (c < 256) {
            mText[mDeepTextLoc++ & bitmask] = (uint8_t)c;
            *pOut++ = (uint8_t)c;
        }
        else {
            uint16_t j = (uint16_t)(c - 255 + cDeepThreshold);
            uint16_t i = (uint16_t)(mDeepTextLoc - DeepDecodePosition() - 1);
            while (j--) {
                uint8_t ch = mText[i++ & bitmask];
                mText[mDeepTextLoc++ & bitmask] = ch;
                *pOut++ = ch;
            }
        }
    }
    mDeepTextLoc = (uint16_t)((mDeepTextLoc + 60) & bitmask);

    return true;
}

// ---- modes 5/6: HEAVY1/HEAVY2 ----

uint16_t cDmsUnpacker::MakeTableRecurse(sMakeTableState& s) {
    uint16_t i = 0;

    if (s.err)
        return 0;

    if (s.len == s.depth) {
        while (++s.c < s.n) {
            if (s.blen[s.c] == s.len) {
                i = s.codeword;
                s.codeword = (uint16_t)(s.codeword + s.bit);
                if (s.codeword > s.tblsiz) { s.err = 1; return 0; }
                while (i < s.codeword) s.tbl[i++] = (uint16_t)s.c;
                return (uint16_t)s.c;
            }
        }
        s.c = -1;
        s.len++;
        s.bit = (uint16_t)(s.bit >> 1);
    }

    s.depth++;
    if (s.depth < s.maxdepth) {
        MakeTableRecurse(s);
        MakeTableRecurse(s);
    }
    else if (s.depth > 32) {
        s.err = 2;
        return 0;
    }
    else {
        i = (uint16_t)(s.avail++);
        if (i >= 2 * s.n - 1) { s.err = 3; return 0; }
        mHeavyLeft[i] = MakeTableRecurse(s);
        mHeavyRight[i] = MakeTableRecurse(s);
        if (s.codeword >= s.tblsiz) { s.err = 4; return 0; }
        if (s.depth == s.maxdepth) s.tbl[s.codeword++] = i;
    }
    s.depth--;
    return i;
}

int cDmsUnpacker::MakeTable(int pNChar, const uint8_t* pBitLen, int pTableBits, uint16_t* pTable) {
    sMakeTableState s{};
    s.n = s.avail = pNChar;
    s.blen = pBitLen;
    s.tbl = pTable;
    s.tblsiz = 1 << pTableBits;
    s.bit = (uint16_t)(s.tblsiz / 2);
    s.maxdepth = pTableBits + 1;
    s.depth = s.len = 1;
    s.c = -1;
    s.codeword = 0;
    s.err = 0;

    MakeTableRecurse(s);
    if (s.err) return s.err;
    MakeTableRecurse(s);
    if (s.err) return s.err;
    if (s.codeword != s.tblsiz) return 5;
    return 0;
}

bool cDmsUnpacker::HeavyReadTreeC() {
    uint16_t n = GetBits(9); DropBits(9);
    if (n > 0) {
        for (uint16_t i = 0; i < n; i++) { mHeavyCLen[i] = (uint8_t)GetBits(5); DropBits(5); }
        for (uint16_t i = n; i < cHeavyNC; i++) mHeavyCLen[i] = 0;
        if (MakeTable(cHeavyNC, mHeavyCLen, 12, mHeavyCTable)) return false;
    }
    else {
        n = GetBits(9); DropBits(9);
        for (uint16_t i = 0; i < cHeavyNC; i++) mHeavyCLen[i] = 0;
        for (uint16_t i = 0; i < 4096; i++) mHeavyCTable[i] = n;
    }
    return true;
}

bool cDmsUnpacker::HeavyReadTreeP() {
    uint16_t n = GetBits(5); DropBits(5);
    if (n > 0) {
        for (uint16_t i = 0; i < n; i++) { mHeavyPtLen[i] = (uint8_t)GetBits(4); DropBits(4); }
        for (uint16_t i = n; i < mHeavyNp; i++) mHeavyPtLen[i] = 0;
        if (MakeTable(mHeavyNp, mHeavyPtLen, 8, mHeavyPtTable)) return false;
    }
    else {
        n = GetBits(5); DropBits(5);
        for (uint16_t i = 0; i < mHeavyNp; i++) mHeavyPtLen[i] = 0;
        for (uint16_t i = 0; i < 256; i++) mHeavyPtTable[i] = n;
    }
    return true;
}

uint16_t cDmsUnpacker::HeavyDecodeC() {
    uint16_t j = mHeavyCTable[GetBits(12)];
    if (j < cHeavyNC) {
        DropBits(mHeavyCLen[j]);
    }
    else {
        DropBits(12);
        uint16_t i = GetBits(16);
        uint16_t m = 0x8000;
        do {
            j = (i & m) ? mHeavyRight[j] : mHeavyLeft[j];
            m = (uint16_t)(m >> 1);
        } while (j >= cHeavyNC);
        DropBits(mHeavyCLen[j] - 12);
    }
    return j;
}

uint16_t cDmsUnpacker::HeavyDecodeP() {
    uint16_t j = mHeavyPtTable[GetBits(8)];
    if (j < mHeavyNp) {
        DropBits(mHeavyPtLen[j]);
    }
    else {
        DropBits(8);
        uint16_t i = GetBits(16);
        uint16_t m = 0x8000;
        do {
            j = (i & m) ? mHeavyRight[j] : mHeavyLeft[j];
            m = (uint16_t)(m >> 1);
        } while (j >= mHeavyNp);
        DropBits(mHeavyPtLen[j] - 8);
    }

    if (j != mHeavyNp - 1) {
        if (j > 0) {
            uint16_t bits = (uint16_t)(j - 1);
            j = (uint16_t)(GetBits(bits) | (1u << bits));
            DropBits(bits);
        }
        mHeavyLastLen = j;
    }

    return mHeavyLastLen;
}

bool cDmsUnpacker::UnpackHeavy(const uint8_t* pIn, uint8_t* pOut, uint8_t pFlags, uint16_t pOrigSize) {
    uint16_t bitmask;
    if (pFlags & 8) { mHeavyNp = 15; bitmask = 0x1fff; }
    else { mHeavyNp = 14; bitmask = 0x0fff; }

    InitBitBuf(pIn);

    if (pFlags & 2) {
        if (!HeavyReadTreeC()) return false;
        if (!HeavyReadTreeP()) return false;
    }

    uint8_t* outEnd = pOut + pOrigSize;
    while (pOut < outEnd) {
        uint16_t c = HeavyDecodeC();
        if (c < 256) {
            mText[mHeavyTextLoc++ & bitmask] = (uint8_t)c;
            *pOut++ = (uint8_t)c;
        }
        else {
            uint16_t j = (uint16_t)(c - 253); // OFFSET
            uint16_t i = (uint16_t)(mHeavyTextLoc - HeavyDecodeP() - 1);
            while (j--) {
                uint8_t ch = mText[i++ & bitmask];
                mText[mHeavyTextLoc++ & bitmask] = ch;
                *pOut++ = ch;
            }
        }
    }

    return true;
}

// ---- per-track dispatch ----

bool cDmsUnpacker::UnpackTrack(const uint8_t* pIn, uint8_t* pOut, uint16_t pPkLen2, uint16_t pUnpkLen, uint8_t pCMode, uint8_t pFlags) {
    bool ok = true;

    switch (pCMode) {
    case 0:
        memcpy(pOut, pIn, pUnpkLen);
        break;
    case 1:
        ok = UnpackRLE(pIn, pOut, pUnpkLen);
        break;
    case 2: {
        std::vector<uint8_t> lzOut(pPkLen2);
        ok = UnpackQuick(pIn, lzOut.data(), pPkLen2);
        if (ok) ok = UnpackRLE(lzOut.data(), pOut, pUnpkLen);
        break;
    }
    case 3: {
        std::vector<uint8_t> lzOut(pPkLen2);
        ok = UnpackMedium(pIn, lzOut.data(), pPkLen2);
        if (ok) ok = UnpackRLE(lzOut.data(), pOut, pUnpkLen);
        break;
    }
    case 4: {
        std::vector<uint8_t> lzOut(pPkLen2);
        ok = UnpackDeep(pIn, lzOut.data(), pPkLen2);
        if (ok) ok = UnpackRLE(lzOut.data(), pOut, pUnpkLen);
        break;
    }
    case 5:
    case 6: {
        std::vector<uint8_t> heavyOut((std::max)(pPkLen2, pUnpkLen));
        uint8_t heavyFlags = (pCMode == 5) ? (uint8_t)(pFlags & 7) : (uint8_t)(pFlags | 8);
        ok = UnpackHeavy(pIn, heavyOut.data(), heavyFlags, pPkLen2);
        if (ok) {
            if (pFlags & 4)
                ok = UnpackRLE(heavyOut.data(), pOut, pUnpkLen);
            else
                memcpy(pOut, heavyOut.data(), pUnpkLen);
        }
        break;
    }
    default:
        return false;
    }

    if (ok && !(pFlags & 1))
        InitDecrunchers();

    return ok;
}

// ---- archive parsing ----

std::vector<uint8_t> cDmsUnpacker::Unpack(const std::vector<uint8_t>& pInput) {
    std::vector<uint8_t> result;

    if (pInput.size() < cHeaderLen)
        return result;

    const uint8_t* header = pInput.data();
    if (header[0] != 'D' || header[1] != 'M' || header[2] != 'S' || header[3] != '!')
        return result;

    uint16_t hcrc = (uint16_t)((header[cHeaderLen - 2] << 8) | header[cHeaderLen - 1]);
    if (hcrc != CreateCRC(header + 4, cHeaderLen - 6))
        return result;

    uint16_t geninfo = (uint16_t)((header[10] << 8) | header[11]);
    uint16_t disktype = (uint16_t)((header[50] << 8) | header[51]);

    if (disktype == 7)
        return result; // FMS archive, not a disk image

    if (geninfo & 2)
        return result; // Encrypted - no password prompt available here

    mText.assign(cTextBufferLen, 0);
    mDeepFreq.assign(cDeepT + 1, 0);
    mDeepPrnt.assign(cDeepT + cDeepNChar, 0);
    mDeepSon.assign(cDeepT, 0);
    mHeavyLeft.assign(2 * cHeavyNC - 1, 0);
    mHeavyRight.assign(2 * cHeavyNC - 1 + 9, 0);

    InitDecrunchers();

    size_t pos = cHeaderLen;

    while (pos + cTrackHeaderLen <= pInput.size()) {
        const uint8_t* th = pInput.data() + pos;

        if (th[0] != 'T' || th[1] != 'R')
            break; // no more tracks

        uint16_t thcrc = (uint16_t)((th[cTrackHeaderLen - 2] << 8) | th[cTrackHeaderLen - 1]);
        if (CreateCRC(th, cTrackHeaderLen - 2) != thcrc)
            break; // corrupt track header - treat as end of valid data, same as xdms

        uint16_t number = (uint16_t)((th[2] << 8) | th[3]);
        uint16_t pklen1 = (uint16_t)((th[6] << 8) | th[7]);
        uint16_t pklen2 = (uint16_t)((th[8] << 8) | th[9]);
        uint16_t unpklen = (uint16_t)((th[10] << 8) | th[11]);
        uint8_t flags = th[12];
        uint8_t cmode = th[13];
        uint16_t usum = (uint16_t)((th[14] << 8) | th[15]);
        uint16_t dcrc = (uint16_t)((th[16] << 8) | th[17]);

        pos += cTrackHeaderLen;

        if (pos + pklen1 > pInput.size())
            break;

        const uint8_t* trackData = pInput.data() + pos;
        bool crcOk = (CreateCRC(trackData, pklen1) == dcrc);

        pos += pklen1;

        // track 80 = FILEID.DIZ, 0xffff = banner, and a 1024-byte track 0
        // is a fake advertising boot block - none of that is disk content.
        if (number < 80 && unpklen > 2048) {
            if (crcOk) {
                std::vector<uint8_t> padded(trackData, trackData + pklen1);
                padded.resize((size_t)pklen1 + 32, 0); // safety margin for the bit-reader's lookahead

                std::vector<uint8_t> trackOut(unpklen, 0);

                // UnpackTrack() itself calls InitDecrunchers() when its decode
                // succeeds and !(flags&1), so the dictionary/tree state for
                // the next track is handled as part of that call.
                bool decodeOk = UnpackTrack(padded.data(), trackOut.data(), pklen2, unpklen, cmode, flags);

                if (decodeOk && CalcCheckSum(trackOut.data(), unpklen) == usum) {
                    size_t offset = (size_t)number * unpklen;
                    if (result.size() < offset + unpklen)
                        result.resize(offset + unpklen, 0);
                    memcpy(result.data() + offset, trackOut.data(), unpklen);
                }
                else if (!decodeOk && !(flags & 1)) {
                    // Decode itself failed, so UnpackTrack never reached its
                    // own reset - still honor the flag so the next track
                    // isn't fed a stale dictionary. (If decodeOk was true but
                    // the checksum mismatched, the reset already happened.)
                    InitDecrunchers();
                }
                // else: leave this track as zeros in the output and keep going,
                // rather than failing the whole disk over one bad track.
            }
            else if (!(flags & 1)) {
                InitDecrunchers();
            }
        }
    }

    return result;
}

std::vector<uint8_t> DecompressDMS(const std::vector<uint8_t>& pInput) {
    cDmsUnpacker unpacker;
    return unpacker.Unpack(pInput);
}
