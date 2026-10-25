// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"

// cfba0e55-5d15-446a-965b-ebf03145b000
static const GUID GUIDPlugin_Firy = { 0xcfba0e55, 0x5d15, 0x446a,{ 0x96, 0x5b, 0xeb, 0xf0, 0x31, 0x45, 0xb0, 0x00 } };

HINSTANCE g_hModuleInstance = 0;

extern std::wstring s2ws(const std::string& str);
extern void InstallPluginOptions();

// Exceptions must never cross into Opus: firy throws on some corrupt-image
// paths (and std::bad_alloc is always possible), and an exception escaping a
// plugin export takes the whole Opus process down with it.
template <class tResult, class tFunc> tResult Guarded(tResult pFailure, tFunc pFunc) {
    try {
        return pFunc();
    }
    catch (...) {
        return pFailure;
    }
}

#define PLUGIN(h) ((cFiryPluginData*)(h))

extern "C"
{
    __declspec(dllexport) bool VFS_IdentifyW(LPVFSPLUGININFOW lpVFSInfo);
    __declspec(dllexport) bool VFS_ReadDirectoryW(HANDLE hData, LPVFSFUNCDATA lpVFSData, LPVFSREADDIRDATAW lpRDD);

    __declspec(dllexport) HANDLE VFS_Create(LPGUID pGuid);
    __declspec(dllexport) void VFS_Destroy(HANDLE hData);

    __declspec(dllexport) HANDLE VFS_CreateFileW(HANDLE hData, LPVFSFUNCDATA lpVFSData, LPWSTR lpszPath, DWORD dwMode, DWORD dwFileAttr, DWORD dwFlags, LPFILETIME lpFT);

    __declspec(dllexport) bool VFS_ReadFile(HANDLE hData, LPVFSFUNCDATA lpVFSData, HANDLE hFile, LPVOID lpData, DWORD dwSize, LPDWORD lpdwReadSize);
    __declspec(dllexport) void VFS_CloseFile(HANDLE hData, LPVFSFUNCDATA lpVFSData, HANDLE hFile);

    __declspec(dllexport) int VFS_ContextVerbW(HANDLE hData, LPVFSFUNCDATA lpVFSData, LPVFSCONTEXTVERBDATAW lpVerbData);
    __declspec(dllexport) UINT VFS_BatchOperationW(HANDLE hData, LPVFSFUNCDATA lpVFSData, LPWSTR lpszPath, LPVFSBATCHDATAW lpBatchData);
    __declspec(dllexport) bool VFS_PropGetW(HANDLE hVFSData, vfsProperty propId, LPVOID lpPropData, LPVOID lpData1, LPVOID lpData2, LPVOID lpData3);

    __declspec(dllexport) bool VFS_GetFreeDiskSpaceW(HANDLE hData, LPVFSFUNCDATA lpFuncData, LPWSTR lpszPath, unsigned __int64* piFreeBytesAvailable, unsigned __int64* piTotalBytes, unsigned __int64* piTotalFreeBytes);

    __declspec(dllexport) HANDLE VFS_FindFirstFileW(HANDLE hData, LPVFSFUNCDATA lpVFSData, LPWSTR lpszPath, LPWIN32_FIND_DATA lpwfdData, HANDLE hAbortEvent);
    __declspec(dllexport) bool VFS_FindNextFileW(HANDLE hData, LPVFSFUNCDATA lpVFSData, HANDLE hFind, LPWIN32_FIND_DATA lpwfdData);
    __declspec(dllexport) void VFS_FindClose(HANDLE hData, HANDLE hFind);

    __declspec(dllexport) bool VFS_USBSafe(LPOPUSUSBSAFEDATA pUSBSafeData);
    __declspec(dllexport) bool VFS_Init(LPVFSINITDATA pInitData);
    __declspec(dllexport) void VFS_Uninit();
};


extern "C" int WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID) {
    g_hModuleInstance = hInstance;
    return 1;
}

// Initialise plugin
bool VFS_Init(LPVFSINITDATA pInitData) {
    // firy's globals (gFiry/gResources/...) are constructed via static init;
    // only the save behaviour needs adjusting for the plugin.
    return Guarded(false, [&] { InstallPluginOptions(); return true; });
}

void VFS_Uninit() {
}

bool VFS_IdentifyW(LPVFSPLUGININFOW lpVFSInfo) {
    // Initialise plugin information
    lpVFSInfo->idPlugin = GUIDPlugin_Firy;
    lpVFSInfo->dwFlags = VFSF_CANCONFIGURE | VFSF_NONREENTRANT;
    lpVFSInfo->dwCapabilities = VFSCAPABILITY_CASESENSITIVE | VFSCAPABILITY_POSTCOPYREREAD;

    std::wstring Extensions;
    for (auto& ext : firy::cFiry::getKnownExtensions()) {
        if (!Extensions.empty())
            Extensions += L";";
        Extensions += L"." + s2ws(ext);
    }
    Extensions += L";.dms"; // unpacked to an in-memory ADF ourselves, firy doesn't know DMS

    StringCchCopyW(lpVFSInfo->lpszHandleExts, lpVFSInfo->cchHandleExtsMax, Extensions.c_str());
    StringCchCopyW(lpVFSInfo->lpszName, lpVFSInfo->cchNameMax, L"Amiga/C64 Disk Images (firy)");
    StringCchCopyW(lpVFSInfo->lpszDescription, lpVFSInfo->cchDescriptionMax, L"Amiga (ADF/HDF/DMS), Commodore 64 (D64/D71/D81) and FAT/ISO9660 disk image support");
    StringCchCopyW(lpVFSInfo->lpszCopyright, lpVFSInfo->cchCopyrightMax, L"(c) A. Schumm (gh0stless)");
    StringCchCopyW(lpVFSInfo->lpszURL, lpVFSInfo->cchURLMax, L"github.com/segrax/firy");

    return true;
}

bool VFS_USBSafe(LPOPUSUSBSAFEDATA pUSBSafeData) {
    return true;
}

HANDLE VFS_Create(LPGUID pGuid) {
    return Guarded((HANDLE)0, [&] { return (HANDLE)new cFiryPluginData(); });
}

void VFS_Destroy(HANDLE hData) {
    Guarded(0, [&] { delete PLUGIN(hData); return 0; });
}

bool VFS_ReadFile(HANDLE hData, LPVFSFUNCDATA lpVFSData, HANDLE hFile, LPVOID lpData, DWORD dwSize, LPDWORD lpdwReadSize) {
    if (!hData)
        return false;
    return Guarded(false, [&] { return PLUGIN(hData)->ReadFile(hFile, dwSize, (std::uint8_t*)lpData, lpdwReadSize); });
}

void VFS_CloseFile(HANDLE hData, LPVFSFUNCDATA lpVFSData, HANDLE hFile) {
    if (hData)
        Guarded(0, [&] { PLUGIN(hData)->CloseFile(hFile); return 0; });
}

HANDLE VFS_CreateFileW(HANDLE hData, LPVFSFUNCDATA lpVFSData, LPWSTR lpszPath, DWORD dwMode, DWORD dwFileAttr, DWORD dwFlags, LPFILETIME lpFT) {
    if (!hData)
        return INVALID_HANDLE_VALUE;
    return Guarded(INVALID_HANDLE_VALUE, [&] { return PLUGIN(hData)->OpenFile(lpszPath); });
}

bool VFS_ReadDirectoryW(HANDLE hData, LPVFSFUNCDATA lpFuncData, LPVFSREADDIRDATAW lpRDD) {
    if (!hData)
        return false;
    return Guarded(false, [&] { return PLUGIN(hData)->ReadDirectory(lpRDD); });
}

int VFS_ContextVerbW(HANDLE hData, LPVFSFUNCDATA lpVFSData, LPVFSCONTEXTVERBDATAW lpVerbData) {
    if (!hData)
        return VFSCVRES_FAIL;
    return Guarded((int)VFSCVRES_FAIL, [&] { return PLUGIN(hData)->ContextVerb(lpVerbData); });
}

UINT VFS_BatchOperationW(HANDLE hData, LPVFSFUNCDATA lpVFSData, LPWSTR lpszPath, LPVFSBATCHDATAW lpBatchData) {
    if (!hData)
        return VFSBATCHRES_ABORT;
    return Guarded((UINT)VFSBATCHRES_ABORT, [&] { return PLUGIN(hData)->BatchOperation(lpszPath, lpBatchData); });
}

bool VFS_PropGetW(HANDLE hData, vfsProperty propId, LPVOID lpPropData, LPVOID lpData1, LPVOID lpData2, LPVOID lpData3) {
    if (!hData)
        return false;
    return Guarded(false, [&] { return PLUGIN(hData)->PropGet(propId, lpPropData, lpData1, lpData2, lpData3); });
}

bool VFS_GetFreeDiskSpaceW(HANDLE hData, LPVFSFUNCDATA lpFuncData, LPWSTR lpszPath, unsigned __int64* piFreeBytesAvailable, unsigned __int64* piTotalBytes, unsigned __int64* piTotalFreeBytes) {

    if (!hData)
        return false;

    return Guarded(false, [&] {
        auto plugin = PLUGIN(hData);
        size_t free = plugin->TotalFreeBlocks(lpszPath);

        if (piFreeBytesAvailable)
            *piFreeBytesAvailable = free;

        if (piTotalFreeBytes)
            *piTotalFreeBytes = free;

        if (piTotalBytes)
            *piTotalBytes = plugin->TotalDiskBlocks(lpszPath);

        return true;
    });
}

HANDLE VFS_FindFirstFileW(HANDLE hData, LPVFSFUNCDATA lpVFSData, LPWSTR lpszPath, LPWIN32_FIND_DATA lpwfdData, HANDLE hAbortEvent) {
    if (!hData)
        return INVALID_HANDLE_VALUE;
    return Guarded(INVALID_HANDLE_VALUE, [&] {
        auto find = PLUGIN(hData)->FindFirstFile(lpszPath, lpwfdData, hAbortEvent);
        return find ? (HANDLE)find : INVALID_HANDLE_VALUE;
    });
}

bool VFS_FindNextFileW(HANDLE hData, LPVFSFUNCDATA lpVFSData, HANDLE hFind, LPWIN32_FIND_DATA lpwfdData) {
    if (!hData || !hFind || hFind == INVALID_HANDLE_VALUE)
        return false;
    return Guarded(false, [&] { return PLUGIN(hData)->FindNextFile((cFiryFindData*)hFind, lpwfdData); });
}

void VFS_FindClose(HANDLE hData, HANDLE hFind) {
    if (hData && hFind && hFind != INVALID_HANDLE_VALUE)
        Guarded(0, [&] { PLUGIN(hData)->FindClose((cFiryFindData*)hFind); return 0; });
}
