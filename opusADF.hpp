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
    bool mIsPartitionLevel = false;
    size_t mIndex = 0;
    std::wregex mFindMask;
};

class cFiryPluginData {

    // The container currently loaded (an ADF/D64/FAT/ISO9660/DMS image, or
    // the .hdf a partitioned image was found in).
    firy::spImage mImage;
    std::wstring mImagePath;
    size_t mLastError;

    // mImage only exists in memory (an unpacked DMS) - nothing can be written back
    bool mReadOnly = false;

    // "Replace all" was picked in the replace dialog during the current batch
    bool mReplaceAll = false;

    // RDB-partitioned .hdf support: LoadFile() populates mPartitions when
    // an RDB is found. Until a specific partition name is navigated into,
    // browsing sees mPartitions as a virtual folder listing rather than a
    // real firy image (mImage stays null in that case).
    bool mIsPartitioned = false;
    std::vector<sPartitionInfo> mPartitions;
    firy::spImage mPartitionImage;
    std::string mActivePartition;

protected:

    LPVFSFILEDATAHEADER GetVFSforEntry(const firy::spNode& pEntry, HANDLE pHeap);
    void GetWfdForEntry(const firy::spNode& pEntry, LPWIN32_FIND_DATA pData);

    LPVFSFILEDATAHEADER GetVFSforPartition(const sPartitionInfo& pPart, HANDLE pHeap);
    void GetWfdForPartition(const sPartitionInfo& pPart, LPWIN32_FIND_DATA pData);

    FILETIME GetFileTime(const firy::helpers::sDateTime& pDT);
    firy::helpers::sDateTime ToDateTime(FILETIME pFT);

    bool LoadFile(const std::wstring& pPath);
    bool OpenPartition(const std::string& pName);

    // Resolves a full VFS path down to (the firy image responsible for it,
    // the path inside that image). If pPath names the RDB partition-list
    // level itself (an .hdf with no partition segment navigated into yet),
    // pOutImage comes back null and the caller is expected to work with
    // mPartitions directly. Returns false only on an outright failure to
    // load/navigate (missing/corrupt file, unknown partition name, ...).
    bool ResolveImage(const std::wstring& pPath, firy::spImage& pOutImage, std::string& pOutInner);

    std::wstring GetInnerPath(const std::wstring& pPath);

    void Invalidate();
    bool IsWritable(const firy::spImage& pImage) const;
    bool NewEntryName(const firy::spImage& pImage, const std::wstring& pName, const bool pIsDir, std::string& pOut, int& pD64Type);
    firy::spNode FindEntry(const firy::spImage& pImage, const firy::spDirectory& pDir, const std::string& pName);
    size_t BlocksNeeded(const firy::spImage& pImage, const size_t pBytes);
    size_t BlocksFree(const firy::spImage& pImage);

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
