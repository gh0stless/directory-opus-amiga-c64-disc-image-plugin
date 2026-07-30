// opusADF.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include <string>
#include <iostream>

DOpusPluginHelperFunction DOpus;

std::wstring s2ws(const std::string& str) {
    if (str.empty())
        return std::wstring();

    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), 0, 0);
    std::wstring result(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0], len);
    return result;
}

std::string ws2s(const std::wstring& wstr) {
    if (wstr.empty())
        return std::string();

    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), 0, 0, 0, 0);
    std::string result(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], len, 0, 0);
    return result;
}

/**
 * Builds a safe regex from a DOS-style wildcard mask (only '*' and '?' are
 * treated specially, everything else - including regex metacharacters that
 * are common in real Amiga filenames like '(', ')', '+', '[' - is escaped
 * literally). Never throws: a name is not a trusted regex pattern, unlike
 * the previous "(." + name + ")" construction which could take an
 * std::regex_error straight across the DLL boundary into Opus on a name
 * like "Setup(Beta" or "C++". Also fixes exact (non-wildcard) lookups,
 * which the old "always one leading char" pattern could never match.
 */
std::regex BuildWildcardRegex(const std::wstring& pMask) {
    std::string pattern = "^";

    for (char ch : ws2s(pMask)) {
        if (ch == '*') {
            pattern += ".*";
        }
        else if (ch == '?') {
            pattern += '.';
        }
        else if (strchr(".^$|()[]{}+\\", ch)) {
            pattern += '\\';
            pattern += ch;
        }
        else {
            pattern += ch;
        }
    }

    pattern += "$";

    try {
        return std::regex(pattern, std::regex::icase);
    }
    catch (const std::regex_error&) {
        // Should be unreachable given the escaping above, but never let a
        // malformed mask take the whole process down.
        return std::regex(std::string());
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

cFiryPluginData::cFiryPluginData() {
    mLastError = 0;
}

cFiryPluginData::~cFiryPluginData() {

}

FILETIME cFiryPluginData::GetFileTime(const firy::helpers::sDateTime& pDT) {
    SYSTEMTIME LocalTime = {}, UTCTime;
    LocalTime.wYear = (WORD)pDT.year;
    LocalTime.wMonth = (WORD)pDT.month;
    LocalTime.wDay = (WORD)pDT.days;
    LocalTime.wHour = (WORD)pDT.hour;
    LocalTime.wMinute = (WORD)pDT.mins;
    LocalTime.wSecond = (WORD)pDT.secs;

    TzSpecificLocalTimeToSystemTime(NULL, &LocalTime, &UTCTime);
    FILETIME ft;
    SystemTimeToFileTime(&UTCTime, &ft);
    return ft;
}

firy::helpers::sDateTime cFiryPluginData::ToDateTime(FILETIME pFT) {
    SYSTEMTIME UTCTime, LocalTime;
    FileTimeToSystemTime(&pFT, &UTCTime);
    SystemTimeToTzSpecificLocalTime(NULL, &UTCTime, &LocalTime);

    return firy::helpers::sDateTime(LocalTime.wYear, LocalTime.wMonth, LocalTime.wDay, LocalTime.wHour, LocalTime.wMinute, LocalTime.wSecond);
}

void cFiryPluginData::GetWfdForEntry(const firy::spNode& pEntry, LPWIN32_FIND_DATA pData) {
    auto filename = s2ws(pEntry->nameGet());
    StringCchCopyW(pData->cFileName, MAX_PATH, filename.c_str());

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
 * Split a full VFS path into the on-disk image file and the path inside it,
 * by looking for the earliest occurrence of any extension firy knows about
 * (plus .dms, which firy doesn't know). Also checks for an RDB partition
 * table, since real .hdf files almost always have one.
 */
bool cFiryPluginData::LoadFile(const std::wstring& pPath) {
    std::wstring Lower = pPath;
    std::transform(Lower.begin(), Lower.end(), Lower.begin(), ::towlower);

    // Extensions firy can open directly, plus .dms, which we unpack to an
    // in-memory ADF ourselves before handing it to firy (firy doesn't know
    // about DMS and isn't touched to teach it).
    auto Extensions = firy::cFiry::getKnownExtensions();
    Extensions.push_back("dms");

    size_t BestPos = std::wstring::npos;
    size_t BestEnd = std::wstring::npos;
    std::wstring BestExt;

    for (auto& ext : Extensions) {
        std::wstring DotExt = L"." + s2ws(ext);
        size_t Pos = Lower.find(DotExt);
        if (Pos == std::wstring::npos)
            continue;

        if (BestPos == std::wstring::npos || Pos < BestPos) {
            BestPos = Pos;
            BestEnd = Pos + DotExt.size();
            BestExt = s2ws(ext);
        }
    }

    if (BestEnd == std::wstring::npos)
        return false;

    std::wstring ImagePath = pPath.substr(0, BestEnd);

    if ((mImage || mIsPartitioned) && mImagePath == ImagePath)
        return true;

    mImage.reset();
    mIsPartitioned = false;
    mPartitions.clear();
    mPartitionImage.reset();
    mActivePartition.clear();

    // Real hard-disk images are almost always RDB-partitioned (unlike
    // floppy .adf images), so check for that first regardless of
    // extension - it's cheap (a handful of small reads).
    auto partitions = ParseRDB(ws2s(ImagePath));
    if (!partitions.empty()) {
        mIsPartitioned = true;
        mPartitions = std::move(partitions);
        mImagePath = ImagePath;
        return true;
    }

    firy::spImage image;

    if (BestExt == L"dms") {
        auto raw = firy::gResources->FileRead(ws2s(ImagePath));
        if (!raw)
            return false;

        std::vector<uint8_t> rawBytes(raw->data(), raw->data() + raw->size());
        auto adfBytes = DecompressDMS(rawBytes);
        if (adfBytes.empty())
            return false;

        auto source = std::make_shared<cMemorySource>(std::move(adfBytes));
        image = firy::gFiry->openImageFile<firy::images::cADF>(source);
    }
    else {
        image = firy::gFiry->openImage(ws2s(ImagePath));
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

        auto source = std::make_shared<cOffsetSource>(ws2s(mImagePath), part.mOffset, part.mSize);
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

    if (!mIsPartitioned) {
        pOutImage = mImage;
        pOutInner = InnerPathUtf8(pPath);
        return true;
    }

    auto inner = GetInnerPath(pPath);
    auto depth = tokenize(inner, L"/");

    if (depth.empty()) {
        // The .hdf root itself - the caller works with mPartitions directly.
        pOutImage.reset();
        pOutInner.clear();
        return true;
    }

    if (!OpenPartition(ws2s(depth[0])))
        return false;

    std::wstring rest;
    for (size_t i = 1; i < depth.size(); i++)
        rest += L"/" + depth[i];

    pOutImage = mPartitionImage;
    pOutInner = ws2s(rest);
    return true;
}

std::wstring cFiryPluginData::GetInnerPath(const std::wstring& pPath) {
    std::wstring Remainder = pPath;

    if (Remainder.size() >= mImagePath.size() && !Remainder.compare(0, mImagePath.size(), mImagePath))
        Remainder = Remainder.substr(mImagePath.size());

    std::replace(Remainder.begin(), Remainder.end(), L'\\', L'/');
    return Remainder;
}

std::string cFiryPluginData::InnerPathUtf8(const std::wstring& pPath) {
    return ws2s(GetInnerPath(pPath));
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

    auto handle = (sFiryFileHandle*)hFile;
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

    return ToRead > 0;
}

HANDLE cFiryPluginData::OpenFile(std::wstring pPath) {

    firy::spImage image;
    std::string inner;
    if (!ResolveImage(pPath, image, inner) || !image)
        return 0;

    auto file = image->filesystemFile(inner);
    if (!file)
        return 0;

    auto handle = new sFiryFileHandle();
    handle->mFile = file;
    handle->mBuffer = file->read();

    return (HANDLE)handle;
}

void cFiryPluginData::CloseFile(HANDLE hFile) {
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
    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_STATUSTEXT, (DWORD_PTR)"Deleting");

    if (lpBatchData->hAbortEvent && WaitForSingleObject(lpBatchData->hAbortEvent, 0) == WAIT_OBJECT_0)
        return 1;

    firy::spImage image;
    std::string inner;
    if (!ResolveImage(pFile, image, inner) || !image)
        return 1; // can't delete a partition itself

    auto node = image->filesystemNode(inner);
    if (!node)
        return 1;

    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETFILENAME, (DWORD_PTR)node->nameGet().c_str());
    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETFILESIZE, (DWORD_PTR)node->sizeInBytesGet());

    // firy's sNode::remove() already tears down whole sub-trees recursively
    if (!node->remove())
        return 1;

    DOpus.AddFunctionFileChange(lpBatchData->lpFuncData, true, OPUSFILECHANGE_REMDIR, pFile.c_str());
    return 0;
}

cFiryFindData* cFiryPluginData::FindFirstFile(LPTSTR lpszPath, LPWIN32_FIND_DATA lpwfdData, HANDLE hAbortEvent) {

    cFiryFindData* findData = new cFiryFindData();

    if (!LoadFile(lpszPath))
        return findData;

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
        return findData;

    findData->mFindMask = BuildWildcardRegex(FileMask);

    if (!image) {
        findData->mIsPartitionLevel = true;

        while (findData->mIndex < mPartitions.size()) {
            auto& part = mPartitions[findData->mIndex];
            findData->mIndex++;

            if (std::regex_match(part.mName, findData->mFindMask)) {
                GetWfdForPartition(part, lpwfdData);
                break;
            }
        }
        return findData;
    }

    auto parentDir = image->filesystemPath(parentInner);
    if (parentDir) {
        findData->mDirectory = parentDir;

        while (findData->mIndex < parentDir->mNodes.size()) {
            auto& node = parentDir->mNodes[findData->mIndex];
            findData->mIndex++;

            if (std::regex_match(node->nameGet(), findData->mFindMask)) {
                GetWfdForEntry(node, lpwfdData);
                break;
            }
        }
    }

    return findData;
}

bool cFiryPluginData::FindNextFile(cFiryFindData* pFindData, LPWIN32_FIND_DATA lpwfdData) {

    if (pFindData->mIsPartitionLevel) {
        while (pFindData->mIndex < mPartitions.size()) {
            auto& part = mPartitions[pFindData->mIndex];
            pFindData->mIndex++;

            if (std::regex_match(part.mName, pFindData->mFindMask)) {
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

        if (std::regex_match(node->nameGet(), pFindData->mFindMask)) {
            GetWfdForEntry(node, lpwfdData);
            return true;
        }
    }

    return false;
}

void cFiryPluginData::FindClose(cFiryFindData* pFindData) {
    delete pFindData;
}

int cFiryPluginData::ImportFile(LPVFSBATCHDATAW lpBatchData, const std::wstring& pFile, const std::wstring& pPath) {

    auto Depth = tokenize(pPath, L"\\/");
    std::wstring ItemName = Depth.empty() ? pPath : Depth.back();

    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETFILENAME, (DWORD_PTR)ItemName.c_str());

    auto content = firy::gResources->FileRead(ws2s(pPath));
    if (!content)
        return 1;

    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETFILESIZE, (DWORD_PTR)content->size());

    firy::spImage image;
    std::string inner;
    if (!ResolveImage(pFile, image, inner) || !image)
        return 1; // can't import directly onto the partition-list level

    auto parentDir = image->filesystemPath(inner);
    if (!parentDir)
        return 1;

    auto newFile = image->filesystemFileCreate(ws2s(ItemName));
    if (!newFile)
        return 1;

    newFile->mContent = content;
    newFile->sizeInBytesSet(content->size());

    FILETIME ft;
    HANDLE hSrc = CreateFile(pPath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ::GetFileTime(hSrc, 0, 0, &ft);
    CloseHandle(hSrc);

    auto dt = ToDateTime(ft);
    newFile->timeWriteSet(dt);

    if (!parentDir->add(newFile))
        return 1;

    std::wstring Final = pFile;
    if (Final[Final.size() - 1] != L'\\')
        Final += L"\\";
    Final += ItemName;

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

    while (1) {
        if (::FindNextFile(dhandle, &fdata)) {
            results.push_back(std::wstring(fdata.cFileName));
        }
        else {
            if (GetLastError() == ERROR_NO_MORE_FILES) {
                break;
            }
            else {
                FindClose(dhandle);
                return results;
            }
        }
    }

    FindClose(dhandle);
    return results;
}

int cFiryPluginData::ImportPath(LPVFSBATCHDATAW lpBatchData, const std::wstring& pFile, const std::wstring& pPath) {

    std::wstring FinalLocalPath = pPath;
    if (FinalLocalPath[FinalLocalPath.size() - 1] != L'\\')
        FinalLocalPath += L"\\";

    auto Depth = tokenize(pPath, L"\\/");
    std::wstring DirName = Depth.empty() ? pPath : Depth.back();

    firy::spImage image;
    std::string inner;
    if (!ResolveImage(pFile, image, inner) || !image)
        return 1; // can't create a folder directly on the partition-list level

    auto parentDir = image->filesystemPath(inner);
    if (!parentDir)
        return 1;

    auto newDir = image->filesystemDirectoryCreate(ws2s(DirName));
    if (!newDir)
        // Filesystem doesn't support sub-directories (eg. a flat C64 disk)
        return 1;

    if (!parentDir->add(newDir))
        return 1;

    std::wstring FinalVfsPath = pFile;
    if (FinalVfsPath[FinalVfsPath.size() - 1] != L'\\')
        FinalVfsPath += L"\\";
    FinalVfsPath += DirName;

    DOpus.AddFunctionFileChange(lpBatchData->lpFuncData, true, OPUSFILECHANGE_MAKEDIR, FinalVfsPath.c_str());

    auto contents = directoryList(FinalLocalPath + L"*.*");

    for (auto& File : contents) {
        if (File == L"." || File == L"..")
            continue;

        Import(lpBatchData, FinalVfsPath, FinalLocalPath + File);
    }

    return 0;
}

int cFiryPluginData::Import(LPVFSBATCHDATAW lpBatchData, const std::wstring& pFile, const std::wstring& pPath) {

    if (lpBatchData->hAbortEvent && WaitForSingleObject(lpBatchData->hAbortEvent, 0) == WAIT_OBJECT_0)
        return 1;

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

    if (!firy::gResources->FileSave(ws2s(pDest), content))
        return 2;

    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_STEPBYTES, (DWORD_PTR)content->size());

    HANDLE hDst = CreateFile(pDest.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    auto ft = GetFileTime(pEntry->timeWriteGet());
    SetFileTime(hDst, 0, 0, &ft);
    CloseHandle(hDst);

    DOpus.AddFunctionFileChange(lpBatchData->lpFuncData, false, OPUSFILECHANGE_CREATE, lpBatchData->pszDestPath);
    return 0;
}

int cFiryPluginData::ExtractPath(LPVFSBATCHDATAW lpBatchData, const std::wstring& pPath, const std::wstring& pDest) {

    firy::spImage image;
    std::string inner;
    if (!ResolveImage(pPath, image, inner))
        return 1;

    int result = 0;

    if (!image) {
        // Extracting the .hdf root: recurse into each partition.
        for (auto& part : mPartitions) {
            std::wstring ChildPath = pPath;
            if (ChildPath[ChildPath.size() - 1] != L'\\')
                ChildPath += L"\\";
            ChildPath += s2ws(part.mName);

            result |= Extract(lpBatchData, ChildPath, pDest);
        }
        return result;
    }

    auto dir = image->filesystemPath(inner);
    if (!dir)
        return 1;

    for (auto& node : dir->mNodes) {

        std::wstring ChildPath = pPath;
        if (ChildPath[ChildPath.size() - 1] != L'\\')
            ChildPath += L"\\";
        ChildPath += s2ws(node->nameGet());

        result |= Extract(lpBatchData, ChildPath, pDest);
    }

    return result;
}

int cFiryPluginData::Extract(LPVFSBATCHDATAW lpBatchData, const std::wstring& pFile, const std::wstring& pDest) {
    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_STATUSTEXT, (DWORD_PTR)"Copying");

    if (lpBatchData->hAbortEvent && WaitForSingleObject(lpBatchData->hAbortEvent, 0) == WAIT_OBJECT_0)
        return 1;

    firy::spImage image;
    std::string inner;
    if (!ResolveImage(pFile, image, inner))
        return 1;

    auto Depth = tokenize(pFile, L"\\/");
    std::wstring ItemName = Depth.empty() ? pFile : Depth.back();

    std::wstring FinalName = pDest;
    if (FinalName[FinalName.size() - 1] != '\\')
        FinalName += '\\';
    FinalName += ItemName;

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
        CreateDirectory(FinalName.c_str(), 0);
        result = ExtractPath(lpBatchData, pFile, FinalName);
        DOpus.AddFunctionFileChange(lpBatchData->lpFuncData, false, OPUSFILECHANGE_CREATE, lpBatchData->pszDestPath);

        if (!result) {
            HANDLE hDst = CreateFile(FinalName.c_str(), FILE_WRITE_ATTRIBUTES, 0, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
            auto ft = GetFileTime(node->timeWriteGet());
            SetFileTime(hDst, 0, 0, &ft);
            CloseHandle(hDst);
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
    for (auto& part : mPartitions) {
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
