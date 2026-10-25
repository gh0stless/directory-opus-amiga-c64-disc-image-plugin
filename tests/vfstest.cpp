// Drives opusAmigaDisc.dll through its VFS exports, the way Directory Opus does.
// Usage: vfstest <dll> <command> args... [-- <command> args...]
//   list <vfspath>                 list a folder
//   add [--ask] <vfsdest> <local>...  copy local files/folders into the image
//   del <vfspath>...               delete entries
//   extract <localdest> <vfspath>... copy out of the image
//   find <vfspath-with-mask>       VFS_FindFirstFile/NextFile
//   read <vfspath> <localfile>     VFS_CreateFile/ReadFile (4 KB reads)
//   free <vfspath>                 VFS_GetFreeDiskSpace
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

#define VFSPLUGINVERSION 2
typedef const BYTE* LPCBYTE;
#include "vfs plugins.h"

typedef bool (*tInit)(LPVFSINITDATA);
typedef HANDLE (*tCreate)(LPGUID);
typedef void (*tDestroy)(HANDLE);
typedef bool (*tReadDir)(HANDLE, LPVFSFUNCDATA, LPVFSREADDIRDATAW);
typedef UINT (*tBatch)(HANDLE, LPVFSFUNCDATA, LPWSTR, LPVFSBATCHDATAW);
typedef HANDLE (*tFindFirst)(HANDLE, LPVFSFUNCDATA, LPWSTR, LPWIN32_FIND_DATAW, HANDLE);
typedef bool (*tFindNext)(HANDLE, LPVFSFUNCDATA, HANDLE, LPWIN32_FIND_DATAW);
typedef void (*tFindClose)(HANDLE, HANDLE);
typedef HANDLE (*tCreateFile)(HANDLE, LPVFSFUNCDATA, LPWSTR, DWORD, DWORD, DWORD, LPFILETIME);
typedef bool (*tReadFile)(HANDLE, LPVFSFUNCDATA, HANDLE, LPVOID, DWORD, LPDWORD);
typedef void (*tCloseFile)(HANDLE, LPVFSFUNCDATA, HANDLE);
typedef bool (*tFree)(HANDLE, LPVFSFUNCDATA, LPWSTR, unsigned __int64*, unsigned __int64*, unsigned __int64*);

static void out(const std::wstring& s) {
	int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), 0, 0, 0, 0);
	std::string u(len, 0);
	WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &u[0], len, 0, 0);
	fwrite(u.data(), 1, u.size(), stdout);
	fflush(stdout);
}

static std::vector<wchar_t> multi(const std::vector<std::wstring>& items) {
	std::vector<wchar_t> r;
	for (auto& s : items) { r.insert(r.end(), s.begin(), s.end()); r.push_back(0); }
	r.push_back(0);
	return r;
}

int wmain(int argc, wchar_t** argv) {
	HMODULE dll = LoadLibraryW(argv[1]);
	if (!dll) { out(L"cannot load dll\n"); return 99; }
#define FN(t, n) auto n = (t)GetProcAddress(dll, #n)
	FN(tInit, VFS_Init); FN(tCreate, VFS_Create); FN(tDestroy, VFS_Destroy);
	FN(tReadDir, VFS_ReadDirectoryW); FN(tBatch, VFS_BatchOperationW);
	FN(tFindFirst, VFS_FindFirstFileW); FN(tFindNext, VFS_FindNextFileW); FN(tFindClose, VFS_FindClose);
	FN(tCreateFile, VFS_CreateFileW); FN(tReadFile, VFS_ReadFile); FN(tCloseFile, VFS_CloseFile);
	FN(tFree, VFS_GetFreeDiskSpaceW);

	VFSINITDATA init = { sizeof(init) };
	VFS_Init(&init);
	HANDLE h = VFS_Create(0);
	int rc = 0;

	for (int i = 2; i < argc;) {
		std::wstring cmd = argv[i++];
		std::vector<std::wstring> args;
		while (i < argc && wcscmp(argv[i], L"--")) args.push_back(argv[i++]);
		if (i < argc) ++i;

		if (cmd == L"list") {
			VFSREADDIRDATAW rd = { sizeof(rd) };
			rd.lpszPath = (LPWSTR)args[0].c_str();
			rd.vfsReadOp = VFSREAD_NORMAL;
			rd.hMemHeap = GetProcessHeap();
			if (!VFS_ReadDirectoryW(h, 0, &rd)) { out(L"LIST FAILED\n"); rc = 1; continue; }
			for (auto hdr = rd.lpFileData; hdr; hdr = hdr->lpNext) {
				auto fd = (LPVFSFILEDATAW)(hdr + 1);
				SYSTEMTIME st = {}; FileTimeToSystemTime(&fd->wfdData.ftLastWriteTime, &st);
				wchar_t line[400];
				swprintf(line, 400, L"%s\t%u\t%s\t%04d-%02d-%02d\n", (fd->wfdData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? L"DIR" : L"FILE",
					fd->wfdData.nFileSizeLow, fd->wfdData.cFileName, st.wYear, st.wMonth, st.wDay);
				out(line);
			}
		}
		else if (cmd == L"add" || cmd == L"del" || cmd == L"extract") {
			bool ask = !args.empty() && args[0] == L"--ask";
			if (ask) args.erase(args.begin());
			std::wstring path = args[0];
			std::vector<std::wstring> files(args.begin() + 1, args.end());
			if (cmd == L"del") files.insert(files.begin(), path);
			auto list = multi(files);
			std::vector<int> results(files.size(), -1);
			VFSBATCHDATAW bd = { sizeof(bd) };
			bd.uiOperation = cmd == L"add" ? VFSBATCHOP_ADD : cmd == L"del" ? VFSBATCHOP_DELETE : VFSBATCHOP_EXTRACT;
			bd.iNumFiles = (int)files.size();
			bd.pszFiles = list.data();
			bd.piResults = results.data();
			bd.pszDestPath = (LPWSTR)path.c_str();
			bd.dwFlags = ask ? BATCHF_COPY_ASK_REPLACE : 0;
			UINT res = VFS_BatchOperationW(h, 0, (LPWSTR)path.c_str(), &bd);
			wchar_t line[64]; swprintf(line, 64, L"%s result=%u per-file:", cmd.c_str(), res); out(line);
			for (int r : results) { swprintf(line, 64, L" %d", r); out(line); }
			out(L"\n");
			if (res != VFSBATCHRES_COMPLETE) rc = 1;
		}
		else if (cmd == L"find") {
			WIN32_FIND_DATAW fd;
			HANDLE f = VFS_FindFirstFileW(h, 0, (LPWSTR)args[0].c_str(), &fd, 0);
			if (f == INVALID_HANDLE_VALUE) { out(L"find: no match\n"); continue; }
			do { out(L"found\t" + std::wstring(fd.cFileName) + L"\n"); } while (VFS_FindNextFileW(h, 0, f, &fd));
			VFS_FindClose(h, f);
		}
		else if (cmd == L"read") {
			DeleteFileW(args[1].c_str());	// never leave an older result behind
			HANDLE f = VFS_CreateFileW(h, 0, (LPWSTR)args[0].c_str(), 0, 0, 0, 0);
			if (f == INVALID_HANDLE_VALUE || !f) { out(L"read: open failed\n"); rc = 1; continue; }
			HANDLE o = CreateFileW(args[1].c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
			char buf[4096]; DWORD got; bool ok = true; size_t total = 0;
			for (;;) {
				if (!VFS_ReadFile(h, 0, f, buf, sizeof(buf), &got)) { ok = false; break; }
				if (!got) break;
				DWORD w; WriteFile(o, buf, got, &w, 0); total += got;
			}
			CloseHandle(o);
			VFS_CloseFile(h, 0, f);
			wchar_t line[64]; swprintf(line, 64, L"read ok=%d bytes=%zu\n", ok, total); out(line);
			if (!ok) rc = 1;
		}
		else if (cmd == L"free") {
			unsigned __int64 a = 0, t = 0, f = 0;
			bool ok = VFS_GetFreeDiskSpaceW(h, 0, (LPWSTR)args[0].c_str(), &a, &t, &f);
			wchar_t line[96]; swprintf(line, 96, L"free ok=%d free=%llu total=%llu\n", ok, f, t); out(line);
		}
	}

	VFS_Destroy(h);
	return rc;
}
