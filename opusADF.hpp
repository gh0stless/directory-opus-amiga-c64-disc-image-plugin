/**
 * Read cursor for a file that has been opened via VFS_CreateFileW.
 * firy hands back the whole file content in one shot (sFile::read()),
 * so we buffer it once on open and slice it out across the repeated
 * VFS_ReadFile calls Opus makes while copying.
 */
struct sFiryFileHandle {
    firy::spFile mFile;
    firy::spBuffer mBuffer;
    size_t mOffset = 0;
};

class cFiryFindData {

public:
    firy::spDirectory mDirectory;
    size_t mIndex = 0;
    std::regex mFindMask;
};

class cFiryPluginData {

    firy::spImage mImage;
    std::wstring mImagePath;
    size_t mLastError;

protected:

    LPVFSFILEDATAHEADER GetVFSforEntry(const firy::spNode& pEntry, HANDLE pHeap);
    void GetWfdForEntry(const firy::spNode& pEntry, LPWIN32_FIND_DATA pData);

    FILETIME GetFileTime(const firy::helpers::sDateTime& pDT);
    firy::helpers::sDateTime ToDateTime(FILETIME pFT);

    bool LoadFile(const std::wstring& pPath);

    std::wstring GetInnerPath(const std::wstring& pPath);
    std::string InnerPathUtf8(const std::wstring& pPath);

public:
    cFiryPluginData();
    ~cFiryPluginData();

    bool ReadDirectory(LPVFSREADDIRDATAW lpRDD);
    bool ReadFile(HANDLE hFile, size_t pBytes, std::uint8_t* pBuffer, LPDWORD pReadSize);

    HANDLE OpenFile(std::wstring pPath);
    void CloseFile(HANDLE hFile);

    size_t TotalFreeBlocks(const std::wstring& pFile);
    size_t TotalDiskBlocks(const std::wstring& pFile);

    int Delete(LPVFSBATCHDATAW lpBatchData, const std::wstring& pPath, const std::wstring& pFile);

    cFiryFindData* FindFirstFile(LPTSTR lpszPath, LPWIN32_FIND_DATA lpwfdData, HANDLE hAbortEvent);
    bool FindNextFile(cFiryFindData* pFindData, LPWIN32_FIND_DATA lpwfdData);
    void FindClose(cFiryFindData* pFindData);

    int Import(LPVFSBATCHDATAW lpBatchData, const std::wstring& pFile, const std::wstring& pPath);
    int ImportFile(LPVFSBATCHDATAW lpBatchData, const std::wstring& pFile, const std::wstring& pPath);
    int ImportPath(LPVFSBATCHDATAW lpBatchData, const std::wstring& pFile, const std::wstring& pPath);

    int Extract(LPVFSBATCHDATAW lpBatchData, const std::wstring& pFile, const std::wstring& pDest);
    int ExtractFile(LPVFSBATCHDATAW lpBatchData, const firy::spNode& pEntry, const std::wstring& pDest);
    int ExtractPath(LPVFSBATCHDATAW lpBatchData, const std::wstring& pPath, const std::wstring& pDest);

    int ContextVerb(LPVFSCONTEXTVERBDATAW lpVerbData);
    UINT BatchOperation(LPTSTR lpszPath, LPVFSBATCHDATAW lpBatchData);
    bool PropGet(vfsProperty propId, LPVOID lpPropData, LPVOID lpData1, LPVOID lpData2, LPVOID lpData3);

};
