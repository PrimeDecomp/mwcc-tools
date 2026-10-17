/* What MSL's Win32 layer takes from the SDK besides gc2's include/compiler/win32.h: the ANSI names it calls by their
 * generic ones, TCHAR's, and a few more functions, types and constants (their values the SDK's). */
#ifndef MWCC_WASM_WIN32_EXTRA_H
#define MWCC_WASM_WIN32_EXTRA_H

#ifdef __cplusplus
extern "C" {
#endif

typedef char TCHAR;
typedef char* LPTSTR;
typedef const char* LPCTSTR;
typedef WIN32_FIND_DATAA WIN32_FIND_DATA;
typedef STARTUPINFOA STARTUPINFO;
typedef long LONG_PTR;
typedef void* HLOCAL;

#define _T(x) x
#define _tcscpy strcpy
#define _tcscat strcat
#define _itot _itoa

#define CreateFile CreateFileA
#define CreateProcess CreateProcessA
#define DeleteFile DeleteFileA
#define FindFirstFile FindFirstFileA
#define FreeEnvironmentStrings FreeEnvironmentStringsA
#define GetCurrentDirectory GetCurrentDirectoryA
#define MoveFile MoveFileA
#define RemoveDirectory RemoveDirectoryA
#define SetCurrentDirectory SetCurrentDirectoryA
#define SetEnvironmentVariable SetEnvironmentVariableA

#define CREATE_NEW 1
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define STD_INPUT_HANDLE ((DWORD)-10)
#define FILE_TYPE_UNKNOWN 0
#define FILE_TYPE_DISK 1
#define FILE_TYPE_CHAR 2
#define FILE_TYPE_PIPE 3
#define TIME_ZONE_ID_UNKNOWN 0
#define LMEM_FIXED 0
#define LMEM_ZEROINIT 0x40
#define LPTR (LMEM_FIXED | LMEM_ZEROINIT)

typedef struct _TIME_ZONE_INFORMATION {
    LONG Bias;
    WCHAR StandardName[32];
    SYSTEMTIME StandardDate;
    LONG StandardBias;
    WCHAR DaylightName[32];
    SYSTEMTIME DaylightDate;
    LONG DaylightBias;
} TIME_ZONE_INFORMATION, *LPTIME_ZONE_INFORMATION;

HANDLE __stdcall GetCurrentProcess(void);
DWORD __stdcall GetFileType(HANDLE hFile);
HLOCAL __stdcall LocalAlloc(UINT uFlags, SIZE_T uBytes);
HLOCAL __stdcall LocalFree(HLOCAL hMem);
BOOL __stdcall MoveFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName);
BOOL __stdcall SetCurrentDirectoryA(LPCSTR lpPathName);
BOOL __stdcall SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue);
void __stdcall Sleep(DWORD dwMilliseconds);
BOOL __stdcall TerminateProcess(HANDLE hProcess, UINT uExitCode);

char* _itoa(int value, char* str, int radix);

#ifdef __cplusplus
}
#endif

#endif
