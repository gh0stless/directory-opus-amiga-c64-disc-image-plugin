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

/**
 * firy options for the plugin: write every completed change straight back to
 * the image file, but never flush a half-done change when an image is
 * dropped (after a failed operation the plugin discards its cached image).
 */
class cPluginOptions : public firy::cOptions {
public:
    virtual firy::spOptionResponse savechanges(firy::pImage pImage, const std::string& pMessage) override {
        return std::make_shared<firy::cOptionResponse>(false);
    }
    virtual firy::spOptionResponse savechangesExit(firy::pImage pImage, const std::string& pMessage) override {
        return std::make_shared<firy::cOptionResponse>(true);
    }
    virtual firy::spOptions clone() override {
        return std::make_shared<cPluginOptions>(*this);
    }
};

void InstallPluginOptions() {
    if (!std::dynamic_pointer_cast<cPluginOptions>(firy::gOptionsDefault))
        firy::gOptionsDefault = std::make_shared<cPluginOptions>();
}

cFiryPluginData::cFiryPluginData() {
    mLastError = 0;
    InstallPluginOptions();
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

        // Say why a partition can't be opened (shown in Opus' Description column)
        if (!pPart.isSupported()) {
            auto comment = s2ws(pPart.dosTypeName()) + L" filesystem - not supported yet";
            size_t bytes = (comment.size() + 1) * sizeof(wchar_t);
            if (auto text = (LPWSTR)HeapAlloc(pHeap, 0, bytes)) {
                memcpy(text, comment.c_str(), bytes);
                lpFD->lpszComment = text;
            }
        }

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
    mReadOnly = false;
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
    bool readOnly = false;

    if (BestExt == L"dms") {
        firy::spBuffer raw;
        if (!ReadHostFile(ImagePath, raw))
            return false;

        std::vector<uint8_t> rawBytes(raw->data(), raw->data() + raw->size());
        auto adfBytes = DecompressDMS(rawBytes);
        if (adfBytes.empty())
            return false;

        // The unpacked ADF only lives in memory; there's no way to write
        // changes back into a DMS archive
        auto source = std::make_shared<cMemorySource>(std::move(adfBytes));
        image = firy::gFiry->openImageFile<firy::images::cADF>(source);
        readOnly = true;
    }
    else {
        image = firy::gFiry->openImage(HostPath(ImagePath));
    }

    if (!image)
        return false;

    mImage = image;
    mImagePath = ImagePath;
    mReadOnly = readOnly;
    return true;
}

bool cFiryPluginData::OpenPartition(const std::string& pName) {
    if (mPartitionImage && mActivePartition == pName)
        return true;

    for (auto& part : mPartitions) {
        if (part.mName != pName)
            continue;

        // Listed, but firy can only read AmigaDOS (OFS/FFS) partitions
        if (!part.isSupported())
            return false;

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

/**
 * Whether writes into this image are possible. DMS archives only exist as
 * an in-memory unpacked copy, firy's FAT and ISO9660 support isn't meant for
 * writing, and directory cache (DCFS) Amiga volumes would be left with a
 * stale cache.
 */
bool cFiryPluginData::IsWritable(const firy::spImage& pImage) const {
    if (!pImage)
        return false;

    if (std::dynamic_pointer_cast<firy::images::cD64>(pImage))
        return true;

    auto adf = std::dynamic_pointer_cast<firy::images::cADF>(pImage);
    if (adf)
        return !(pImage == mImage && mReadOnly) && !adf->isDirCache();

    return false;
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

int cFiryPluginData::Delete(LPVFSBATCHDATAW lpBatchData, const std::wstring& pPath, const std::wstring& pFile) {
    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_STATUSTEXT, (DWORD_PTR)L"Deleting");

    if (lpBatchData->hAbortEvent && WaitForSingleObject(lpBatchData->hAbortEvent, 0) == WAIT_OBJECT_0)
        return 1;

    firy::spImage image;
    std::string inner;
    if (!ResolveImage(pFile, image, inner) || !image)
        return 1; // can't delete a partition itself

    if (!IsWritable(image))
        return ERROR_WRITE_PROTECT;

    auto node = image->filesystemNode(inner);
    if (!node)
        return 1;

    auto name = s2ws(node->nameGet());
    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETFILENAME, (DWORD_PTR)name.c_str());
    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETFILESIZE, (DWORD_PTR)node->sizeInBytesGet());

    const bool isDir = node->isDirectory();

    // firy's sDirectory::remove() already tears down whole sub-trees recursively
    if (!node->remove()) {
        Invalidate();
        return 1;
    }

    DOpus.AddFunctionFileChange(lpBatchData->lpFuncData, true, isDir ? OPUSFILECHANGE_REMDIR : OPUSFILECHANGE_DELETE, pFile.c_str());
    return 0;
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

/**
 * Turn a Windows file or folder name into the name it gets inside pImage.
 * Returns false if it can't be stored there (characters the format can't
 * hold, or too long - names aren't silently truncated, since two long names
 * could then collide and one would replace the other).
 */
bool cFiryPluginData::NewEntryName(const firy::spImage& pImage, const std::wstring& pName, const bool pIsDir, std::string& pOut, int& pD64Type) {
    pD64Type = firy::images::d64::eFileType_PRG;

    if (std::dynamic_pointer_cast<firy::images::cD64>(pImage)) {
        if (pIsDir)
            return false; // CBM disks have no folders

        // name.prg / name.seq / name.usr pick the CBM file type, as most C64 tools export them
        std::wstring name = pName;
        static const struct { const wchar_t* mExt; int mType; } types[] = {
            { L".prg", firy::images::d64::eFileType_PRG },
            { L".seq", firy::images::d64::eFileType_SEQ },
            { L".usr", firy::images::d64::eFileType_USR },
        };
        for (auto& type : types) {
            if (name.size() > 4 && !_wcsicmp(name.c_str() + name.size() - 4, type.mExt)) {
                name.resize(name.size() - 4);
                pD64Type = type.mType;
                break;
            }
        }

        pOut.clear();
        for (wchar_t ch : name) {
            // Printable ASCII only, minus what CBM DOS uses as syntax in file names
            if (ch < 0x20 || ch > 0x7E || wcschr(L"\",:=*?", ch))
                return false;
            pOut.push_back((char)toupper((char)ch));
        }
        return !pOut.empty() && pOut.size() <= 16;
    }

    if (std::dynamic_pointer_cast<firy::images::cADF>(pImage)) {
        if (!ImageName(pName, pOut))
            return false;
        // ':' and '/' are path separators on the Amiga
        if (pOut.find_first_of(":/") != std::string::npos)
            return false;
        return !pOut.empty() && pOut.size() <= firy::images::adf::gFilenameMaximumLength;
    }

    return false;
}

/**
 * Blocks a file of pBytes occupies in pImage, including its header and
 * extension blocks on Amiga volumes
 */
size_t cFiryPluginData::BlocksNeeded(const firy::spImage& pImage, const size_t pBytes) {
    if (std::dynamic_pointer_cast<firy::images::cD64>(pImage))
        return pBytes ? (pBytes + 253) / 254 : 1;

    auto adf = std::dynamic_pointer_cast<firy::images::cADF>(pImage);
    if (adf) {
        const size_t perBlock = adf->isFFS() ? firy::images::adf::gBytesPerBlockFFS : firy::images::adf::gBytesPerBlockOFS;
        const size_t data = (pBytes + perBlock - 1) / perBlock;
        const size_t perTable = firy::images::adf::gDataBlocksMax;
        const size_t ext = data > perTable ? (data - perTable + perTable - 1) / perTable : 0;
        return 1 + data + ext;
    }

    return 0;
}

size_t cFiryPluginData::BlocksFree(const firy::spImage& pImage) {
    if (std::dynamic_pointer_cast<firy::images::cD64>(pImage))
        return pImage->filesystemTotalBytesFree() / 254;

    auto adf = std::dynamic_pointer_cast<firy::images::cADF>(pImage);
    if (adf)
        return pImage->filesystemTotalBytesFree() / (adf->isFFS() ? firy::images::adf::gBytesPerBlockFFS : firy::images::adf::gBytesPerBlockOFS);

    return 0;
}

/**
 * Find an existing entry with the name pName in pDir, compared the way the
 * image's filesystem compares names.
 */
firy::spNode cFiryPluginData::FindEntry(const firy::spImage& pImage, const firy::spDirectory& pDir, const std::string& pName) {
    auto adf = std::dynamic_pointer_cast<firy::images::cADF>(pImage);

    for (auto& node : pDir->mNodes) {
        if (adf) {
            if (firy::images::adf::sEntry::namesEqual(node->nameGet(), pName, adf->isInternational()))
                return node;
        }
        else if (firy::str_to_upper(node->nameGet()) == firy::str_to_upper(pName)) {
            return node;
        }
    }
    return 0;
}

int cFiryPluginData::ImportFile(LPVFSBATCHDATAW lpBatchData, const std::wstring& pFile, const std::wstring& pPath) {

    auto Depth = tokenize(pPath, L"\\/");
    std::wstring ItemName = Depth.empty() ? pPath : Depth.back();

    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETFILENAME, (DWORD_PTR)ItemName.c_str());

    firy::spImage image;
    std::string inner;
    if (!ResolveImage(pFile, image, inner) || !image)
        return 1; // can't import directly onto the partition-list level

    if (!IsWritable(image))
        return ERROR_WRITE_PROTECT;

    std::string name;
    int d64Type;
    if (!NewEntryName(image, ItemName, false, name, d64Type))
        return ERROR_INVALID_NAME;

    firy::spBuffer content;
    if (!ReadHostFile(pPath, content))
        return 1;

    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETFILESIZE, (DWORD_PTR)content->size());

    auto parentDir = image->filesystemPath(inner);
    if (!parentDir)
        return 1;

    std::wstring Final = PathJoin(pFile, s2ws(name));

    auto existing = FindEntry(image, parentDir, name);
    if (existing) {
        if (existing->isDirectory())
            return ERROR_ALREADY_EXISTS;

        if (lpBatchData->dwFlags & BATCHF_COPY_ASK_REPLACE && !mReplaceAll) {
            WIN32_FIND_DATA wfdSource = {}, wfdDest = {};
            HANDLE hFind = ::FindFirstFile(pPath.c_str(), &wfdSource);
            if (hFind != INVALID_HANDLE_VALUE)
                ::FindClose(hFind);
            GetWfdForEntry(existing, &wfdDest);

            WCHAR newName[MAX_PATH] = {};
            switch (DOpus.ShowFunctionReplaceDlg(lpBatchData->lpFuncData, pPath.c_str(), &wfdSource, Final.c_str(), &wfdDest, newName, MAX_PATH, 0)) {
            case REPLACERES_REPLACEALL:
                mReplaceAll = true;
                break;
            case REPLACERES_REPLACE:
                break;
            case REPLACERES_SKIP:
            case REPLACERES_SKIPALL:
            case REPLACERES_SKIPIDENTICAL:
                return 0;
            default:
                return ERROR_CANCELLED;
            }
        }

        // Make sure the new file fits in the space the old one frees, so a
        // failed replace doesn't leave the old file deleted
        if (BlocksNeeded(image, content->size()) > BlocksFree(image) + BlocksNeeded(image, existing->sizeInBytesGet()))
            return ERROR_DISK_FULL;

        if (!existing->remove()) {
            Invalidate();
            return 1;
        }
        DOpus.AddFunctionFileChange(lpBatchData->lpFuncData, true, OPUSFILECHANGE_DELETE, Final.c_str());
    }

    auto newFile = image->filesystemFileCreate(name);
    if (!newFile)
        return 1;

    auto d64File = std::dynamic_pointer_cast<firy::images::d64::sFile>(newFile);
    if (d64File)
        d64File->mType = (firy::images::d64::eFileType)d64Type;

    newFile->mContent = content;
    newFile->sizeInBytesSet(content->size());

    HANDLE hSrc = CreateFileW(pPath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hSrc != INVALID_HANDLE_VALUE) {
        FILETIME ft;
        if (::GetFileTime(hSrc, 0, 0, &ft)) {
            auto dt = ToDateTime(ft);
            newFile->timeWriteSet(dt);
        }
        CloseHandle(hSrc);
    }

    if (!parentDir->add(newFile)) {
        Invalidate();
        return ERROR_DISK_FULL;
    }

    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_STEPBYTES, (DWORD_PTR)content->size());
    DOpus.AddFunctionFileChange(lpBatchData->lpFuncData, true, OPUSFILECHANGE_CREATE, Final.c_str());
    return 0;
}

std::vector<std::wstring> directoryList(const std::wstring pPath) {
    WIN32_FIND_DATA fdata;
    HANDLE dhandle;
    std::vector<std::wstring> results;

    if ((dhandle = ::FindFirstFile(pPath.c_str(), &fdata)) == INVALID_HANDLE_VALUE)
        return results;

    results.push_back(std::wstring(fdata.cFileName));

    while (::FindNextFile(dhandle, &fdata))
        results.push_back(std::wstring(fdata.cFileName));

    ::FindClose(dhandle);
    return results;
}

int cFiryPluginData::ImportPath(LPVFSBATCHDATAW lpBatchData, const std::wstring& pFile, const std::wstring& pPath) {

    auto Depth = tokenize(pPath, L"\\/");
    std::wstring DirName = Depth.empty() ? pPath : Depth.back();

    firy::spImage image;
    std::string inner;
    if (!ResolveImage(pFile, image, inner) || !image)
        return 1; // can't create a folder directly on the partition-list level

    if (!IsWritable(image))
        return ERROR_WRITE_PROTECT;

    std::string name;
    int d64Type;
    if (!NewEntryName(image, DirName, true, name, d64Type))
        return ERROR_INVALID_NAME; // includes CBM disks, which have no folders

    auto parentDir = image->filesystemPath(inner);
    if (!parentDir)
        return 1;

    std::wstring FinalVfsPath = PathJoin(pFile, s2ws(name));

    auto existing = FindEntry(image, parentDir, name);
    if (existing) {
        // Copying a folder onto one that's already there merges into it
        if (!existing->isDirectory())
            return ERROR_ALREADY_EXISTS;
        FinalVfsPath = PathJoin(pFile, s2ws(existing->nameGet()));
    }
    else {
        auto newDir = image->filesystemDirectoryCreate(name);
        if (!newDir)
            return 1;

        if (!parentDir->add(newDir)) {
            Invalidate();
            return ERROR_DISK_FULL;
        }

        DOpus.AddFunctionFileChange(lpBatchData->lpFuncData, true, OPUSFILECHANGE_MAKEDIR, FinalVfsPath.c_str());
    }

    int result = 0;
    for (auto& File : directoryList(PathJoin(pPath, L"*.*"))) {
        if (File == L"." || File == L"..")
            continue;

        result = Import(lpBatchData, FinalVfsPath, PathJoin(pPath, File));
        if (result)
            break;
    }

    return result;
}

int cFiryPluginData::Import(LPVFSBATCHDATAW lpBatchData, const std::wstring& pFile, const std::wstring& pPath) {

    if (lpBatchData->hAbortEvent && WaitForSingleObject(lpBatchData->hAbortEvent, 0) == WAIT_OBJECT_0)
        return ERROR_CANCELLED;

    auto Attribs = GetFileAttributes(pPath.c_str());
    if (Attribs == INVALID_FILE_ATTRIBUTES)
        return 1;

    if (Attribs & FILE_ATTRIBUTE_DIRECTORY)
        return ImportPath(lpBatchData, pFile, pPath);

    return ImportFile(lpBatchData, pFile, pPath);
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
        return IsWritable(image) ? image->filesystemTotalBytesFree() : 0;

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
    mReplaceAll = false;

    auto File = lpBatchData->pszFiles;

    for (int i = 0; i < lpBatchData->iNumFiles; ++i) {

        if (lpBatchData->uiOperation == VFSBATCHOP_EXTRACT) {
            lpBatchData->piResults[i] = Extract(lpBatchData, File, lpBatchData->pszDestPath);
        }

        if (lpBatchData->uiOperation == VFSBATCHOP_ADD) {
            lpBatchData->piResults[i] = Import(lpBatchData, lpszPath, File);
        }

        if (lpBatchData->uiOperation == VFSBATCHOP_DELETE) {
            lpBatchData->piResults[i] = Delete(lpBatchData, lpszPath, File);
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

        // Rename/SetAttr/Clipboard aren't implemented; Delete and MakeDir are.
        *Data &= ~(VFSFUNCAVAIL_RENAME | VFSFUNCAVAIL_SETATTR | VFSFUNCAVAIL_CLIPCUT | VFSFUNCAVAIL_CLIPPASTE | VFSFUNCAVAIL_CLIPPASTESHORTCUT | VFSFUNCAVAIL_DUPLICATE);
        return true;
    }

    return false;
}
