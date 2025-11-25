#pragma once

#define _WIN32_WINNT  0x0600
// // Including SDKDDKVer.h defines the highest available Windows platform.
// If you wish to build your application for a previous Windows platform, include WinSDKVer.h and
// set the _WIN32_WINNT macro to the platform you wish to support before including SDKDDKVer.h.
#include <SDKDDKVer.h>

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>
// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <shellapi.h>

#include "resource.h"

#ifdef _DEBUG
inline void DbgPrint(const TCHAR* fmt, ...)
{
    static TCHAR buffer[256];

    va_list args;
    va_start(args, fmt);
    _vstprintf_s(buffer, sizeof(buffer) / sizeof(TCHAR), fmt, args);
    va_end(args);

    OutputDebugString(buffer);
}
#else
inline void DbgPrint(const TCHAR* fmt, ...) {}
#endif
