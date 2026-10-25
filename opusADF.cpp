// opusADF.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include <string>
#include <iostream>

DOpusPluginHelperFunction DOpus;

// Converts a name read straight off a disk image (a filename, a partition
// name, or a path built by concatenating those) into the wide string Opus
// expects.
//
// This is NOT UTF-8. Amiga filenames are native single-byte text: the
// Amiga's own character set is ISO-8859-1/Latin-1 for the accented range
// (firy's cADF entry parser just memcpy's the raw on-disk name bytes into a
// std::string with no charset conversion of its own). Standard Commodore
// PETSCII, as used by D64/D71/D81, has no umlaut characters in its filename
// charset at all in practice, so decoding those bytes as Latin-1 as well is
// at least a harmless, well-defined choice.
//
// Because ISO-8859-1's 256 code points map 1:1 onto Unicode's first 256
// code points, "decoding" it is just a per-byte widen.
std::wstring s2ws(const std::string& str) {
    std::wstring result;
    result.reserve(str.size());
    for (unsigned char c : str)
        result.push_back(static_cast<wchar_t>(c));
    return result;
}

// The reverse of s2ws: a name coming from Opus, as the bytes it has (or
// will have) inside the image. Fails for characters outside Latin-1, which
// can't exist inside an image, so a lookup for them can never match and a
// new entry can't be created with them.
bool ImageName(const std::wstring& pName, std::string& pOut) {
    pOut.clear();
    pOut.reserve(pName.size());
    for (wchar_t ch : pName) {
        if (ch > 0xFF)
            return false;
        pOut.push_back(static_cast<char>(ch));
    }
    return true;
}

// A Windows path for firy's file API. firy opens files through
// std::ifstream/std::filesystem with a narrow std::string, which MSVC
// interprets in the ANSI code page (not UTF-8), so convert to exactly that.
// A path the ANSI code page can't represent (eg. Japanese folder names on a
// German system) falls back to its 8.3 short form, which is plain ASCII.
std::string HostPath(const std::wstring& pPath) {
    auto convert = [](const std::wstring& pWide, bool& pLossy) {
        pLossy = false;
        if (pWide.empty())
            return std::string();

        const UINT codePage = GetACP();
        BOOL usedDefault = FALSE;
        // CP_UTF8 doesn't allow the lossy-check flags (and is never lossy)
        const bool utf8 = (codePage == CP_UTF8);
        int len = WideCharToMultiByte(codePage, utf8 ? 0 : WC_NO_BEST_FIT_CHARS, pWide.c_str(), (int)pWide.size(), 0, 0, 0, utf8 ? 0 : &usedDefault);
        std::string result(len, 0);
        WideCharToMultiByte(codePage, utf8 ? 0 : WC_NO_BEST_FIT_CHARS, pWide.c_str(), (int)pWide.size(), &result[0], len, 0, utf8 ? 0 : &usedDefault);
        pLossy = usedDefault != FALSE;
        return result;
    };

    bool lossy;
    auto result = convert(pPath, lossy);
    if (!lossy)
        return result;

    DWORD len = GetShortPathNameW(pPath.c_str(), 0, 0);
    if (len) {
        std::wstring shortPath(len, 0);
        len = GetShortPathNameW(pPath.c_str(), &shortPath[0], len);
        shortPath.resize(len);
        auto shortResult = convert(shortPath, lossy);
        if (!lossy)
            return shortResult;
    }

    return result;
}

bool ReadHostFile(const std::wstring& pPath, firy::spBuffer& pOut) {
    HANDLE hFile = CreateFileW(pPath.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(hFile, &size) && size.QuadPart < 0x7fffffff;
    pOut = std::make_shared<firy::tBuffer>();
    if (ok && size.QuadPart) {
        pOut->resize((size_t)size.QuadPart);
        DWORD read = 0;
        ok = ::ReadFile(hFile, pOut->data(), (DWORD)size.QuadPart, &read, 0) && read == (DWORD)size.QuadPart;
    }

    CloseHandle(hFile);
    return ok;
}

bool WriteHostFile(const std::wstring& pPath, const firy::spBuffer& pData) {
    HANDLE hFile = CreateFileW(pPath.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    bool ok = true;
    if (pData && pData->size()) {
        DWORD written = 0;
        ok = ::WriteFile(hFile, pData->data(), (DWORD)pData->size(), &written, 0) && written == (DWORD)pData->size();
    }

    CloseHandle(hFile);
    if (!ok)
        DeleteFileW(pPath.c_str());
    return ok;
}

/**
 * Builds a safe regex from a DOS-style wildcard mask (only '*' and '?' are
 * treated specially, everything else - including regex metacharacters that
 * are common in real Amiga filenames like '(', ')', '+', '[' - is escaped
 * literally). Never throws: a name is not a trusted regex pattern.
 */
std::wregex BuildWildcardRegex(const std::wstring& pMask) {
    std::wstring pattern = L"^";

    for (wchar_t ch : pMask) {
        if (ch == L'*') {
            pattern += L".*";
        }
        else if (ch == L'?') {
            pattern += L'.';
        }
        else if (wcschr(L".^$|()[]{}+\\", ch)) {
            pattern += L'\\';
            pattern += ch;
        }
        else {
            pattern += ch;
        }
    }

    pattern += L"$";

    try {
        return std::wregex(pattern, std::regex::icase);
    }
    catch (const std::regex_error&) {
        // Should be unreachable given the escaping above, but never let a
        // malformed mask take the whole process down.
        return std::wregex(std::wstring());
    }
}

std::vector<std::wstring> tokenize(const std::wstring& in, const std::wstring& delim) {
    std::vector<std::wstring> tokens;

    std::wstring::size_type pos_begin, pos_end = 0;
    std::wstring input = in;

    while ((pos_begin = input.find_first_not_of(delim, pos_end)) != std::wstring::npos) {
        pos_end = input.find_first_of(delim, pos_begin);
        if (pos_end == std::wstring::npos) pos_end = input.length();

        tokens.push_back(input.substr(pos_begin, pos_end - pos_begin));
    }

    return tokens;
}

std::wstring PathJoin(const std::wstring& pPath, const std::wstring& pName) {
    std::wstring result = pPath;
    if (!result.empty() && result.back() != L'\\')
        result += L'\\';
    return result + pName;
}

cFiryPluginData::cFiryPluginData() {
    mLastError = 0;
}

cFiryPluginData::~cFiryPluginData() {

}

FILETIME cFiryPluginData::GetFileTime(const firy::helpers::sDateTime& pDT) {
    SYSTEMTIME LocalTime = {}, UTCTime = {};
    LocalTime.wYear = (WORD)pDT.year;
    LocalTime.wMonth = (WORD)pDT.month;
    LocalTime.wDay = (WORD)pDT.days;
    LocalTime.wHour = (WORD)pDT.hour;
    LocalTime.wMinute = (WORD)pDT.mins;
    LocalTime.wSecond = (WORD)pDT.secs;

    FILETIME ft = {};
    // Images without real dates (or with garbage in them) get no date at all
    // rather than whatever was left on the stack
    if (!TzSpecificLocalTimeToSystemTime(NULL, &LocalTime, &UTCTime) || !SystemTimeToFileTime(&UTCTime, &ft))
        return {};
    return ft;
}

firy::helpers::sDateTime cFiryPluginData::ToDateTime(FILETIME pFT) {
    SYSTEMTIME UTCTime, LocalTime;
    if (!FileTimeToSystemTime(&pFT, &UTCTime) || !SystemTimeToTzSpecificLocalTime(NULL, &UTCTime, &LocalTime))
        return firy::helpers::sDateTime();

    return firy::helpers::sDateTime(LocalTime.wYear, LocalTime.wMonth, LocalTime.wDay, LocalTime.wHour, LocalTime.wMinute, LocalTime.wSecond);
}

void cFiryPluginData::GetWfdForEntry(const firy::spNode& pEntry, LPWIN32_FIND_DATA pData) {
    auto filename = s2ws(pEntry->nameGet());
    StringCchCopyW(pData->cFileName, MAX_PATH, filename.c_str());
    pData->cAlternateFileName[0] = 0;

    pData->nFileSizeHigh = 0;
    pData->nFileSizeLow = (DWORD)pEntry->sizeInBytesGet();

    pData->dwFileAttributes = pEntry->isDirectory() ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;

    pData->dwReserved0 = 0;
    pData->dwReserved1 = 0;

    pData->ftCreationTime = {};
    pData->ftLastAccessTime = {};
    pData->ftLastWriteTime = GetFileTime(pEntry->timeWriteGet());
}

LPVFSFILEDATAHEADER cFiryPluginData::GetVFSforEntry(const firy::spNode& pEntry, HANDLE pHeap) {
    LPVFSFILEDATAHEADER lpFDH;

    if (lpFDH = (LPVFSFILEDATAHEADER)HeapAlloc(pHeap, 0, sizeof(VFSFILEDATAHEADER) + sizeof(VFSFILEDATA))) {
        LPVFSFILEDATA lpFD = (LPVFSFILEDATA)(lpFDH + 1);

        lpFDH->cbSize = sizeof(VFSFILEDATAHEADER);
        lpFDH->lpNext = 0;
        lpFDH->iNumItems = 1;
        lpFDH->cbFileDataSize = sizeof(VFSFILEDATA);

        lpFD->dwFlags = 0;
        lpFD->lpszComment = 0;
        lpFD->iNumColumns = 0;
        lpFD->lpvfsColumnData = 0;

        GetWfdForEntry(pEntry, &lpFD->wfdData);
    }

    return lpFDH;
}

void cFiryPluginData::GetWfdForPartition(const sPartitionInfo& pPart, LPWIN32_FIND_DATA pData) {
    auto filename = s2ws(pPart.mName);
    StringCchCopyW(pData->cFileName, MAX_PATH, filename.c_str());
    pData->cAlternateFileName[0] = 0;

    pData->nFileSizeHigh = 0;
    pData->nFileSizeLow = 0;
    pData->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;

    pData->dwReserved0 = 0;
    pData->dwReserved1 = 0;

    pData->ftCreationTime = {};
    pData->ftLastAccessTime = {};
    pData->ftLastWriteTime = {};
}

LPVFSFILEDATAHEADER cFiryPluginData::GetVFSforPartition(const sPartitionInfo& pPart, HANDLE pHeap) {
    LPVFSFILEDATAHEADER lpFDH;

    if (lpFDH = (LPVFSFILEDATAHEADER)HeapAlloc(pHeap, 0, sizeof(VFSFILEDATAHEADER) + sizeof(VFSFILEDATA))) {
        LPVFSFILEDATA lpFD = (LPVFSFILEDATA)(lpFDH + 1);

        lpFDH->cbSize = sizeof(VFSFILEDATAHEADER);
        lpFDH->lpNext = 0;
        lpFDH->iNumItems = 1;
        lpFDH->cbFileDataSize = sizeof(VFSFILEDATA);

        lpFD->dwFlags = 0;
        lpFD->lpszComment = 0;
        lpFD->iNumColumns = 0;
        lpFD->lpvfsColumnData = 0;

        GetWfdForPartition(pPart, &lpFD->wfdData);
    }

    return lpFDH;
}

/**
 * Drop everything cached about the current image, so the next access reads
 * it fresh from disk. Used after a failed write, which may have left firy's
 * in-memory copy half-modified.
 */
void cFiryPluginData::Invalidate() {
    mImage.reset();
    mImagePath.clear();
    mIsPartitioned = false;
    mPartitions.clear();
    mPartitionImage.reset();
    mActivePartition.clear();
}

/**
 * Split a full VFS path into the on-disk image file and the path inside it,
 * by looking for the earliest image extension (any firy knows about, plus
 * .dms) that ends a path component. Also checks for an RDB partition table,
 * since real .hdf files almost always have one.
 */
bool cFiryPluginData::LoadFile(const std::wstring& pPath) {
    std::wstring Lower = pPath;
    std::transform(Lower.begin(), Lower.end(), Lower.begin(), ::towlower);

    // Extensions firy can open directly, plus .dms, which we unpack to an
    // in-memory ADF ourselves before handing it to firy.
    auto Extensions = firy::cFiry::getKnownExtensions();
    Extensions.push_back("dms");

    size_t BestEnd = std::wstring::npos;
    std::wstring BestExt;

    for (auto& ext : Extensions) {
        std::wstring DotExt = L"." + s2ws(ext);

        // Only an extension that ends a path component counts - "game.adf.dms"
        // is a DMS file, "C:\disks.d64\x.d64" has its image at the end
        for (size_t Pos = Lower.find(DotExt); Pos != std::wstring::npos; Pos = Lower.find(DotExt, Pos + 1)) {
            size_t End = Pos + DotExt.size();
            if (End != Lower.size() && Lower[End] != L'\\' && Lower[End] != L'/')
                continue;

            if (End < BestEnd) {
                BestEnd = End;
                BestExt = s2ws(ext);
            }
            break;
        }
    }

    if (BestEnd == std::wstring::npos)
        return false;

    std::wstring ImagePath = pPath.substr(0, BestEnd);

    if ((mImage || mIsPartitioned) && mImagePath == ImagePath)
        return true;

    Invalidate();

    // Real hard-disk images are almost always RDB-partitioned (unlike
    // floppy .adf images), so check for that first regardless of
    // extension - it's cheap (a handful of small reads).
    auto partitions = ParseRDB(HostPath(ImagePath));
    if (!partitions.empty()) {
        mIsPartitioned = true;
        mPartitions = std::move(partitions);
        mImagePath = ImagePath;
        return true;
    }

    firy::spImage image;

    if (BestExt == L"dms") {
        firy::spBuffer raw;
        if (!ReadHostFile(ImagePath, raw))
            return false;

        std::vector<uint8_t> rawBytes(raw->data(), raw->data() + raw->size());
        auto adfBytes = DecompressDMS(rawBytes);
        if (adfBytes.empty())
            return false;

        auto source = std::make_shared<cMemorySource>(std::move(adfBytes));
        image = firy::gFiry->openImageFile<firy::images::cADF>(source);
    }
    else {
        image = firy::gFiry->openImage(HostPath(ImagePath));
    }

    if (!image)
        return false;

    mImage = image;
    mImagePath = ImagePath;
    return true;
}

bool cFiryPluginData::OpenPartition(const std::string& pName) {
    if (mPartitionImage && mActivePartition == pName)
        return true;

    for (auto& part : mPartitions) {
        if (part.mName != pName)
            continue;

        auto source = std::make_shared<cOffsetSource>(HostPath(mImagePath), part.mOffset, part.mSize);
        auto image = firy::gFiry->openImageFile<firy::images::cADF>(source);
        if (!image)
            return false;

        mPartitionImage = image;
        mActivePartition = pName;
        return true;
    }

    return false;
}

bool cFiryPluginData::ResolveImage(const std::wstring& pPath, firy::spImage& pOutImage, std::string& pOutInner) {
    if (!LoadFile(pPath))
        return false;

    std::string inner;
    if (!ImageName(GetInnerPath(pPath), inner))
        return false; // characters that can't exist inside an image

    if (!mIsPartitioned) {
        pOutImage = mImage;
        pOutInner = inner;
        return true;
    }

    std::vector<std::string> depth;
    for (size_t pos = 0; pos <= inner.size();) {
        size_t end = inner.find('/', pos);
        if (end == std::string::npos)
            end = inner.size();
        if (end > pos)
            depth.push_back(inner.substr(pos, end - pos));
        pos = end + 1;
    }

    if (depth.empty()) {
        // The .hdf root itself - the caller works with mPartitions directly.
        pOutImage.reset();
        pOutInner.clear();
        return true;
    }

    if (!OpenPartition(depth[0]))
        return false;

    std::string rest;
    for (size_t i = 1; i < depth.size(); i++)
        rest += "/" + depth[i];

    pOutImage = mPartitionImage;
    pOutInner = rest;
    return true;
}

std::wstring cFiryPluginData::GetInnerPath(const std::wstring& pPath) {
    std::wstring Remainder = pPath;

    if (Remainder.size() >= mImagePath.size() && !Remainder.compare(0, mImagePath.size(), mImagePath))
        Remainder = Remainder.substr(mImagePath.size());

    std::replace(Remainder.begin(), Remainder.end(), L'\\', L'/');
    return Remainder;
}

bool cFiryPluginData::ReadDirectory(LPVFSREADDIRDATAW lpRDD) {

    // Free directory if lister is closing (otherwise ignore free command)
    if (lpRDD->vfsReadOp == VFSREAD_FREEDIRCLOSE)
        return true;

    if (lpRDD->vfsReadOp == VFSREAD_FREEDIR)
        return true;

    // Do nothing if we have no path
    if (!lpRDD->lpszPath || !*lpRDD->lpszPath) {
        mLastError = ERROR_PATH_NOT_FOUND;
        return false;
    }

    firy::spImage image;
    std::string inner;
    if (!ResolveImage(lpRDD->lpszPath, image, inner))
        return false;

    if (!image) {
        // RDB partition-list level: list mPartitions as folders.
        if (lpRDD->vfsReadOp == VFSREAD_CHANGEDIR)
            return true;

        LPVFSFILEDATAHEADER lpLastHeader = 0;
        for (auto& part : mPartitions) {
            LPVFSFILEDATAHEADER lpFDH = GetVFSforPartition(part, lpRDD->hMemHeap);

            if (lpFDH) {
                if (lpLastHeader)
                    lpLastHeader->lpNext = lpFDH;
                else
                    lpRDD->lpFileData = lpFDH;

                lpLastHeader = lpFDH;
            }
        }
        return true;
    }

    auto dir = image->filesystemPath(inner);
    if (!dir)
        return false;

    if (lpRDD->vfsReadOp == VFSREAD_CHANGEDIR)
        return true;

    LPVFSFILEDATAHEADER lpLastHeader = 0;
    for (auto& node : dir->mNodes) {
        LPVFSFILEDATAHEADER lpFDH = GetVFSforEntry(node, lpRDD->hMemHeap);

        if (lpFDH) {
            if (lpLastHeader)
                lpLastHeader->lpNext = lpFDH;
            else
                lpRDD->lpFileData = lpFDH;

            lpLastHeader = lpFDH;
        }
    }

    return true;
}

bool cFiryPluginData::ReadFile(HANDLE hFile, size_t pBytes, std::uint8_t* pBuffer, LPDWORD pReadSize) {

    auto handle = (hFile == INVALID_HANDLE_VALUE) ? nullptr : (sFiryFileHandle*)hFile;
    if (!handle || !handle->mBuffer) {
        *pReadSize = 0;
        return false;
    }

    size_t Remaining = handle->mBuffer->size() - handle->mOffset;
    size_t ToRead = min(pBytes, Remaining);

    if (ToRead)
        memcpy(pBuffer, handle->mBuffer->data() + handle->mOffset, ToRead);

    handle->mOffset += ToRead;
    *pReadSize = (DWORD)ToRead;

    // Like ReadFile(): reaching the end is success with zero bytes, not an error
    return true;
}

HANDLE cFiryPluginData::OpenFile(std::wstring pPath) {

    firy::spImage image;
    std::string inner;
    if (!ResolveImage(pPath, image, inner) || !image)
        return INVALID_HANDLE_VALUE;

    auto file = image->filesystemFile(inner);
    if (!file)
        return INVALID_HANDLE_VALUE;

    auto content = file->read();
    if (!content)
        return INVALID_HANDLE_VALUE;

    auto handle = new sFiryFileHandle();
    handle->mFile = file;
    handle->mBuffer = content;

    return (HANDLE)handle;
}

void cFiryPluginData::CloseFile(HANDLE hFile) {
    if (hFile && hFile != INVALID_HANDLE_VALUE)
        delete (sFiryFileHandle*)hFile;
}

int cFiryPluginData::ContextVerb(LPVFSCONTEXTVERBDATAW lpVerbData) {

    firy::spImage image;
    std::string inner;
    if (!ResolveImage(lpVerbData->lpszPath, image, inner))
        return VFSCVRES_FAIL;

    if (!image)
        return VFSCVRES_DEFAULT; // the .hdf/partition-list level is always a folder

    auto node = image->filesystemNode(inner);
    if (!node)
        return VFSCVRES_FAIL;

    return node->isDirectory() ? VFSCVRES_DEFAULT : VFSCVRES_EXTRACT;
}

cFiryFindData* cFiryPluginData::FindFirstFile(LPTSTR lpszPath, LPWIN32_FIND_DATA lpwfdData, HANDLE hAbortEvent) {

    if (!LoadFile(lpszPath))
        return 0;

    auto Inner = GetInnerPath(lpszPath);
    auto Depth = tokenize(Inner, L"/");

    std::wstring FileMask = Depth.empty() ? L"" : Depth.back();
    if (!Depth.empty())
        Depth.pop_back();

    // Rebuild the parent's full VFS path so ResolveImage() can navigate it
    // the same way as everywhere else (transparently handles the
    // partition-list level too).
    std::wstring ParentPath = mImagePath;
    for (auto& Seg : Depth)
        ParentPath += L"\\" + Seg;

    firy::spImage image;
    std::string parentInner;
    if (!ResolveImage(ParentPath, image, parentInner))
        return 0;

    std::unique_ptr<cFiryFindData> findData(new cFiryFindData());
    findData->mFindMask = BuildWildcardRegex(FileMask);

    if (!image) {
        findData->mIsPartitionLevel = true;
    }
    else {
        findData->mDirectory = image->filesystemPath(parentInner);
        if (!findData->mDirectory)
            return 0;
    }

    // Like FindFirstFile(): no match means no handle at all
    if (!FindNextFile(findData.get(), lpwfdData))
        return 0;

    return findData.release();
}

bool cFiryPluginData::FindNextFile(cFiryFindData* pFindData, LPWIN32_FIND_DATA lpwfdData) {

    if (pFindData->mIsPartitionLevel) {
        while (pFindData->mIndex < mPartitions.size()) {
            auto& part = mPartitions[pFindData->mIndex];
            pFindData->mIndex++;

            if (std::regex_match(s2ws(part.mName), pFindData->mFindMask)) {
                GetWfdForPartition(part, lpwfdData);
                return true;
            }
        }
        return false;
    }

    if (!pFindData->mDirectory)
        return false;

    while (pFindData->mIndex < pFindData->mDirectory->mNodes.size()) {
        auto& node = pFindData->mDirectory->mNodes[pFindData->mIndex];
        pFindData->mIndex++;

        if (std::regex_match(s2ws(node->nameGet()), pFindData->mFindMask)) {
            GetWfdForEntry(node, lpwfdData);
            return true;
        }
    }

    return false;
}

void cFiryPluginData::FindClose(cFiryFindData* pFindData) {
    delete pFindData;
}

int cFiryPluginData::ExtractFile(LPVFSBATCHDATAW lpBatchData, const firy::spNode& pEntry, const std::wstring& pDest) {

    auto file = std::dynamic_pointer_cast<firy::filesystem::sFile>(pEntry);
    if (!file)
        return 1;

    auto content = file->read();
    if (!content)
        return 2;

    if (!WriteHostFile(pDest, content))
        return 2;

    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_STEPBYTES, (DWORD_PTR)content->size());

    auto ft = GetFileTime(pEntry->timeWriteGet());
    if (ft.dwLowDateTime || ft.dwHighDateTime) {
        HANDLE hDst = CreateFileW(pDest.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hDst != INVALID_HANDLE_VALUE) {
            SetFileTime(hDst, 0, 0, &ft);
            CloseHandle(hDst);
        }
    }

    DOpus.AddFunctionFileChange(lpBatchData->lpFuncData, false, OPUSFILECHANGE_CREATE, pDest.c_str());
    return 0;
}

int cFiryPluginData::ExtractPath(LPVFSBATCHDATAW lpBatchData, const std::wstring& pPath, const std::wstring& pDest) {

    firy::spImage image;
    std::string inner;
    if (!ResolveImage(pPath, image, inner))
        return 1;

    // A file that can't be copied (eg. an Amiga "AUX" - a reserved device name
    // on Windows) doesn't stop the rest of the folder; the first error is
    // reported at the end. Only an abort by the user stops early.
    int result = 0;
    auto extractChild = [&](const std::wstring& pName) {
        int childResult = Extract(lpBatchData, PathJoin(pPath, pName), pDest);
        if (childResult && !result)
            result = childResult;
        return childResult != ERROR_CANCELLED;
    };

    if (!image) {
        // Extracting the .hdf root: recurse into each partition.
        auto partitions = mPartitions;
        for (auto& part : partitions) {
            if (!extractChild(s2ws(part.mName)))
                break;
        }
        return result;
    }

    auto dir = image->filesystemPath(inner);
    if (!dir)
        return 1;

    // Copy the list: extracting may reload the image and with it the directory
    auto nodes = dir->mNodes;
    for (auto& node : nodes) {
        if (!extractChild(s2ws(node->nameGet())))
            break;
    }

    return result;
}

int cFiryPluginData::Extract(LPVFSBATCHDATAW lpBatchData, const std::wstring& pFile, const std::wstring& pDest) {
    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_STATUSTEXT, (DWORD_PTR)L"Copying");

    if (lpBatchData->hAbortEvent && WaitForSingleObject(lpBatchData->hAbortEvent, 0) == WAIT_OBJECT_0)
        return ERROR_CANCELLED;

    firy::spImage image;
    std::string inner;
    if (!ResolveImage(pFile, image, inner))
        return 1;

    auto Depth = tokenize(pFile, L"\\/");
    std::wstring ItemName = Depth.empty() ? pFile : Depth.back();
    std::wstring FinalName = PathJoin(pDest, ItemName);

    if (!image) {
        // A partition, presented as a folder - recurse the same way a
        // real directory would.
        CreateDirectory(FinalName.c_str(), 0);
        int result = ExtractPath(lpBatchData, pFile, FinalName);
        DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_NEXTFILE, 0);
        return result;
    }

    auto node = image->filesystemNode(inner);
    if (!node)
        return 1;

    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETFILENAME, (DWORD_PTR)ItemName.c_str());
    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETFILESIZE, (DWORD_PTR)node->sizeInBytesGet());

    int result;
    if (node->isDirectory()) {
        if (!CreateDirectory(FinalName.c_str(), 0) && GetLastError() != ERROR_ALREADY_EXISTS)
            return 2;
        DOpus.AddFunctionFileChange(lpBatchData->lpFuncData, false, OPUSFILECHANGE_MAKEDIR, FinalName.c_str());

        result = ExtractPath(lpBatchData, pFile, FinalName);

        auto ft = GetFileTime(node->timeWriteGet());
        if (!result && (ft.dwLowDateTime || ft.dwHighDateTime)) {
            HANDLE hDst = CreateFile(FinalName.c_str(), FILE_WRITE_ATTRIBUTES, 0, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
            if (hDst != INVALID_HANDLE_VALUE) {
                SetFileTime(hDst, 0, 0, &ft);
                CloseHandle(hDst);
            }
        }
    }
    else {
        result = ExtractFile(lpBatchData, node, FinalName);
    }

    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_NEXTFILE, 0);
    return result;
}

size_t cFiryPluginData::TotalFreeBlocks(const std::wstring& pFile) {

    firy::spImage image;
    std::string inner;
    if (!ResolveImage(pFile, image, inner))
        return 0;

    if (image)
        return image->filesystemTotalBytesFree();

    // .hdf root: sum free space across every partition.
    size_t free = 0;
    auto partitions = mPartitions;
    for (auto& part : partitions) {
        if (OpenPartition(part.mName))
            free += mPartitionImage->filesystemTotalBytesFree();
    }
    return free;
}

size_t cFiryPluginData::TotalDiskBlocks(const std::wstring& pFile) {

    firy::spImage image;
    std::string inner;
    if (!ResolveImage(pFile, image, inner))
        return 0;

    if (image)
        return image->filesystemTotalBytesMax();

    size_t total = 0;
    for (auto& part : mPartitions)
        total += part.mSize;
    return total;
}

UINT cFiryPluginData::BatchOperation(LPTSTR lpszPath, LPVFSBATCHDATAW lpBatchData) {
    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETPERCENT, (DWORD_PTR)0);

    auto result = VFSBATCHRES_COMPLETE;

    auto File = lpBatchData->pszFiles;

    for (int i = 0; i < lpBatchData->iNumFiles; ++i) {

        if (lpBatchData->uiOperation == VFSBATCHOP_EXTRACT) {
            lpBatchData->piResults[i] = Extract(lpBatchData, File, lpBatchData->pszDestPath);
        }

        // Read-only: writing into images is disabled until it's reliable
        if (lpBatchData->uiOperation == VFSBATCHOP_ADD || lpBatchData->uiOperation == VFSBATCHOP_DELETE) {
            lpBatchData->piResults[i] = ERROR_WRITE_PROTECT;
        }

        if (lpBatchData->piResults[i]) {
            result = VFSBATCHRES_ABORT;
            break;
        }

        File += wcslen(File) + 1;
    }

    return result;
}

bool cFiryPluginData::PropGet(vfsProperty propId, LPVOID lpPropData, LPVOID lpData1, LPVOID lpData2, LPVOID lpData3) {

    if (propId == VFSPROP_SHOWTHUMBNAILS) {

        *(bool*)lpPropData = true;

        return true;
    }

    if (propId == VFSPROP_FUNCAVAILABILITY) {
        unsigned __int64* Data = (unsigned __int64*)lpPropData;

        // Read-only: nothing that changes the image
        *Data &= ~(VFSFUNCAVAIL_MOVE | VFSFUNCAVAIL_DELETE | VFSFUNCAVAIL_MAKEDIR | VFSFUNCAVAIL_RENAME | VFSFUNCAVAIL_SETATTR | VFSFUNCAVAIL_CLIPCUT | VFSFUNCAVAIL_CLIPPASTE | VFSFUNCAVAIL_CLIPPASTESHORTCUT | VFSFUNCAVAIL_DUPLICATE | VFSFUNCAVAIL_SETTIME | VFSFUNCAVAIL_SETCOMMENT);
        return true;
    }

    return false;
}
