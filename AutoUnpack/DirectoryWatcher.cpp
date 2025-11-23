#include "DirectoryWatcher.h"

BOOL DirectoryWatcher::Start() {
    if (hThread) return FALSE;

    hDir = CreateFile(
        directory,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (hDir == INVALID_HANDLE_VALUE)
        return FALSE;

    stopFlag = FALSE;

    hThread = CreateThread(
        NULL,
        0,
        ThreadTrampoline,
        this,
        0,
        NULL
    );

    return hThread != NULL;
}

void DirectoryWatcher::Stop() {
    if (!hThread) return;

    stopFlag = TRUE;
    CancelIoEx(hDir, NULL);

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    hThread = NULL;

    if (hDir != INVALID_HANDLE_VALUE) {
        CloseHandle(hDir);
        hDir = INVALID_HANDLE_VALUE;
    }
}

DirectoryWatcher::FileRecord* DirectoryWatcher::FindOrCreateRecord(const TCHAR* name) {
    // Try find existing
    for (int i = 0; i < MAX_TRACKED_FILES; i++) {
        if (files[i].active && _tcsncmp(files[i].name, name, MAX_PATH - 1) == 0)
            return &files[i];
    }
    // Create new slot
    for (int i = 0; i < MAX_TRACKED_FILES; i++) {
        if (!files[i].active) {
            lstrcpyn(files[i].name, name, MAX_PATH);
            files[i].active = TRUE;
            files[i].lastWrite = 0;
            files[i].lastChangeTick = GetTickCount64();
            return &files[i];
        }
    }
    return NULL; // full
}

void DirectoryWatcher::RemoveRecord(const TCHAR* name) {
    for (int i = 0; i < MAX_TRACKED_FILES; i++) {
        if (files[i].active && _tcsncmp(files[i].name, name, MAX_PATH) == 0) {
            files[i].active = FALSE;
            return;
        }
    }
}

void DirectoryWatcher::CheckIdleFiles() {
    ULONGLONG now = GetTickCount64();
    TCHAR full[MAX_PATH];

    for (int i = 0; i < MAX_TRACKED_FILES; i++) {
        if (!files[i].active) continue;

        wsprintf(full, _T("%s\\%s"), directory, files[i].name);

        ULONGLONG write, size;
        if (!GetFileInfo(full, write, size) || write == 0) continue;

        if (size > (ULONGLONG) maxFileSize * 1000000UL) {
            files[i].active = FALSE;
            continue;
        }

        if (write != files[i].lastWrite) {
            files[i].lastWrite = write;
            files[i].lastChangeTick = now;
        }

        if (now - files[i].lastChangeTick >= idleTime) {
            // Trigger callback
            if (callback) callback(directory, files[i].name, full);

            // Deactivate to avoid firing again
            files[i].active = FALSE;
        }
    }
}

void DirectoryWatcher::ThreadProc() {
    alignas(DWORD) BYTE buffer[1024];
    OVERLAPPED ov = {};
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (!ov.hEvent)
        return;

    while (!stopFlag) {
        ResetEvent(ov.hEvent);

        BOOL ok = ReadDirectoryChangesW(
            hDir,
            buffer,
            sizeof(buffer),
            FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
            NULL,
            &ov,
            NULL
        );

        if (!ok)
            break;

        DWORD wait = WaitForSingleObject(ov.hEvent, 100);
        if (wait == WAIT_TIMEOUT) {
            CheckIdleFiles();
            continue;
        }
        if (wait != WAIT_OBJECT_0)
            break;

        DWORD bytes = 0;
        if (!GetOverlappedResult(hDir, &ov, &bytes, FALSE))
            continue;

        ProcessNotifications(buffer, bytes);
        CheckIdleFiles();
    }

    CloseHandle(ov.hEvent);
}

void DirectoryWatcher::ProcessNotifications(BYTE* buffer, DWORD bytes) {
    DWORD offset = 0;

    TCHAR file[MAX_PATH];
    TCHAR full[MAX_PATH];

    while (offset < bytes) {
        FILE_NOTIFY_INFORMATION* p =
            (FILE_NOTIFY_INFORMATION*)(buffer + offset);

        auto len = p->FileNameLength / sizeof(TCHAR);
        _tcsncpy_s(file, MAX_PATH, p->FileName, len);
        file[len] = 0;

        switch (p->Action) {
        case FILE_ACTION_ADDED:
        case FILE_ACTION_RENAMED_NEW_NAME:
        case FILE_ACTION_MODIFIED:
        {
            wsprintf(full, _T("%s\\%s"), directory, file);
            ULONGLONG modified, size;
            if (GetFileInfo(full, modified, size) && size <= (ULONGLONG)maxFileSize * 1000000UL) {
                FileRecord* r = FindOrCreateRecord(file);
                if (r) {
                    r->lastWrite = modified;
                    r->lastChangeTick = GetTickCount64();
                }
            }
        }
        break;

        case FILE_ACTION_REMOVED:
        case FILE_ACTION_RENAMED_OLD_NAME:
            RemoveRecord(file);
            break;
        }

        if (p->NextEntryOffset == 0)
            break;
        offset += p->NextEntryOffset;
    }
}