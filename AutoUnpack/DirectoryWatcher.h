#pragma once

#include "stdafx.h"

#define MAX_TRACKED_FILES 8

// Callback signature
typedef void (*IdleFileCallback)(const TCHAR* folder, const TCHAR* fileName, const TCHAR* fullPath);

class DirectoryWatcher
{
public:
    DirectoryWatcher(
        const TCHAR* directoryPath,
        DWORD idleMilliseconds,
        DWORD maxFileSizeMB,
        IdleFileCallback callbackFunc
    )
        : idleTime(idleMilliseconds),
        maxFileSize(maxFileSizeMB),
        callback(callbackFunc),
        stopFlag(FALSE),
        hDir(INVALID_HANDLE_VALUE),
        hThread(NULL)
    {
        _tcscpy_s(directory, MAX_PATH - 1, directoryPath);
        ZeroMemory(files, sizeof(files));
    }

    ~DirectoryWatcher() {
        Stop();
    }

    BOOL Start();
    void Stop();

private:
    struct FileRecord {
        TCHAR  name[MAX_PATH];
        ULONGLONG lastWrite;
        ULONGLONG lastChangeTick;
        BOOL active;
    };

    TCHAR directory[MAX_PATH];
    DWORD idleTime;
    DWORD maxFileSize;
    IdleFileCallback callback;

    volatile BOOL stopFlag;

    HANDLE hDir;
    HANDLE hThread;

    FileRecord files[MAX_TRACKED_FILES];

    static BOOL GetFileInfo(const TCHAR* path, ULONGLONG& modified, ULONGLONG& size) {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesEx(path, GetFileExInfoStandard, &fad))
            return false;

        ULARGE_INTEGER ui;
        ui.LowPart = fad.ftLastWriteTime.dwLowDateTime;
        ui.HighPart = fad.ftLastWriteTime.dwHighDateTime;
        modified = ui.QuadPart;
        ui.LowPart = fad.nFileSizeLow;
        ui.HighPart = fad.nFileSizeHigh;
        size = ui.QuadPart;
        return true;
    }

    FileRecord* FindOrCreateRecord(const TCHAR* name);
    void RemoveRecord(const TCHAR* name);
    void CheckIdleFiles();

    void ThreadProc();
    void ProcessNotifications(BYTE* buffer, DWORD bytes);

    static DWORD WINAPI ThreadTrampoline(LPVOID param) {
        DirectoryWatcher* self = (DirectoryWatcher*)param;
        self->ThreadProc();
        return 0;
    }
};