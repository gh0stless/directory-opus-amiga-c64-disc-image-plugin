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

/**
 * Split a full VFS path into the on-disk image file and the path inside it,
 * by looking for the earliest occurrence of any extension firy knows about.
 */
bool cFiryPluginData::LoadFile(const std::wstring& pPath) {
    std::wstring Lower = pPath;
    std::transform(Lower.begin(), Lower.end(), Lower.begin(), ::towlower);

    size_t BestPos = std::wstring::npos;
    size_t BestEnd = std::wstring::npos;

    for (auto& ext : firy::cFiry::getKnownExtensions()) {
        std::wstring DotExt = L"." + s2ws(ext);
        size_t Pos = Lower.find(DotExt);
        if (Pos == std::wstring::npos)
            continue;

        if (BestPos == std::wstring::npos || Pos < BestPos) {
            BestPos = Pos;
            BestEnd = Pos + DotExt.size();
        }
    }

    if (BestEnd == std::wstring::npos)
        return false;

    std::wstring ImagePath = pPath.substr(0, BestEnd);

    if (mImage && mImagePath == ImagePath)
        return true;

    auto image = firy::gFiry->openImage(ws2s(ImagePath));
    if (!image)
        return false;

    mImage = image;
    mImagePath = ImagePath;
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

    if (!LoadFile(lpRDD->lpszPath))
        return false;

    auto dir = mImage->filesystemPath(InnerPathUtf8(lpRDD->lpszPath));
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

    if (!LoadFile(pPath))
        return 0;

    auto file = mImage->filesystemFile(InnerPathUtf8(pPath));
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

    if (!LoadFile(lpVerbData->lpszPath))
        return VFSCVRES_FAIL;

    auto node = mImage->filesystemNode(InnerPathUtf8(lpVerbData->lpszPath));
    if (!node)
        return VFSCVRES_FAIL;

    return node->isDirectory() ? VFSCVRES_DEFAULT : VFSCVRES_EXTRACT;
}

int cFiryPluginData::Delete(LPVFSBATCHDATAW lpBatchData, const std::wstring& pPath, const std::wstring& pFile) {
    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_STATUSTEXT, (DWORD_PTR)"Deleting");

    if (lpBatchData->hAbortEvent && WaitForSingleObject(lpBatchData->hAbortEvent, 0) == WAIT_OBJECT_0)
        return 1;

    if (!LoadFile(pFile))
        return 1;

    auto node = mImage->filesystemNode(InnerPathUtf8(pFile));
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

    if (LoadFile(lpszPath)) {

        auto Inner = GetInnerPath(lpszPath);
        auto Depth = tokenize(Inner, L"/");

        std::wstring FileMask = Depth.empty() ? L"" : Depth.back();
        if (!Depth.empty())
            Depth.pop_back();

        std::wstring ParentInner;
        for (auto& Seg : Depth)
            ParentInner += L"/" + Seg;
        if (ParentInner.empty())
            ParentInner = L"/";

        auto parentDir = mImage->filesystemPath(ws2s(ParentInner));
        if (parentDir) {
            findData->mDirectory = parentDir;
            findData->mFindMask = std::regex("(." + ws2s(FileMask) + ")");

            while (findData->mIndex < parentDir->mNodes.size()) {
                auto& node = parentDir->mNodes[findData->mIndex];
                findData->mIndex++;

                if (std::regex_match(node->nameGet(), findData->mFindMask)) {
                    GetWfdForEntry(node, lpwfdData);
                    break;
                }
            }
        }
    }

    return findData;
}

bool cFiryPluginData::FindNextFile(cFiryFindData* pFindData, LPWIN32_FIND_DATA lpwfdData) {

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

    if (!LoadFile(pFile))
        return 1;

    auto parentDir = mImage->filesystemPath(InnerPathUtf8(pFile));
    if (!parentDir)
        return 1;

    auto newFile = mImage->filesystemFileCreate(ws2s(ItemName));
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

    if (!LoadFile(pFile))
        return 1;

    auto parentDir = mImage->filesystemPath(InnerPathUtf8(pFile));
    if (!parentDir)
        return 1;

    auto newDir = mImage->filesystemDirectoryCreate(ws2s(DirName));
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

    if (!LoadFile(pPath))
        return 1;

    auto dir = mImage->filesystemPath(InnerPathUtf8(pPath));
    if (!dir)
        return 1;

    int result = 0;
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

    if (!LoadFile(pFile))
        return 1;

    auto node = mImage->filesystemNode(InnerPathUtf8(pFile));
    if (!node)
        return 1;

    auto Depth = tokenize(pFile, L"\\/");
    std::wstring ItemName = Depth.empty() ? pFile : Depth.back();

    std::wstring FinalName = pDest;
    if (FinalName[FinalName.size() - 1] != '\\')
        FinalName += '\\';
    FinalName += ItemName;

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

    if (!LoadFile(pFile))
        return 0;

    return mImage->filesystemTotalBytesFree();
}

size_t cFiryPluginData::TotalDiskBlocks(const std::wstring& pFile) {

    if (!LoadFile(pFile))
        return 0;

    return mImage->filesystemTotalBytesMax();
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
