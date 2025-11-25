#include "AutoUnpack.h"
#include "DirectoryWatcher.h"

#define MAX_DELIM_ENTRIES 8
#define WM_NMICON WM_USER+1

const TCHAR szTitle[] = _T("AutoUnpack");
const TCHAR szSettingsFile[] = _T("settings.ini");
const TCHAR szCategory[] = _T("Settings");
const TCHAR szDefaultFolders[] = _T("%userprofile%\\Downloads");
const TCHAR szDefaultDelay[] = _T("1000");
const TCHAR szDefaultExtensions[] = _T(".zip,.7z");
const TCHAR szDefaultZipExe[] = _T("7z.exe");
const TCHAR szDefaultMaxFileSize[] = _T("250");
const TCHAR szDelimiter[] = _T(",");

static UINT s_uTaskbarRestart;
static NOTIFYICONDATA s_nid;
static HANDLE s_mutex = NULL;
static HICON s_icon = NULL;
static HICON s_iconDisabled = NULL;

static TCHAR s_settingsFile[MAX_PATH];
static TCHAR s_zipExe[MAX_PATH];

static BOOL s_disabled;
static BOOL s_efficiencyMode;
static BOOL s_autoRun;
static BOOL s_deleteAfter;
static INT s_waitMs;
static INT s_maxFileSizeMB;

static DirectoryWatcher* s_watcher[MAX_DELIM_ENTRIES];
static TCHAR* s_extensions[MAX_DELIM_ENTRIES];


int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPTSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(nCmdShow);

    // Check if another instance is already running
    s_mutex = CreateMutex(NULL, TRUE, szTitle);

    if (s_mutex != NULL && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBox(NULL,
            _T("Another instance of the application is already running."),
            szTitle,
            MB_ICONINFORMATION);
        CloseHandle(s_mutex);
        return 0; // Exit the second instance
    }

    // Load settings
    InitSettings(false);
    SetStartup(s_autoRun);
    SetEfficiencyMode(s_efficiencyMode);

    // Initialize variables
    WNDCLASSEX wcex = {
        sizeof(WNDCLASSEX),
        CS_CLASSDC,
        WndProc, 0L, 0L,
        GetModuleHandle(NULL),
        NULL, NULL, NULL, NULL,
        szTitle, NULL
    };

    // Register window class
    if (!RegisterClassEx(&wcex)) {
        MessageBox(NULL, _T("Call to RegisterClassEx failed!"), szTitle, MB_ICONERROR);
        return 1;
    }

    // Create a hidden window
    HWND hWnd = CreateWindow(
        wcex.lpszClassName, szTitle,
        WS_OVERLAPPEDWINDOW,
        0, 0, 0, 0, NULL, NULL,
        wcex.hInstance, NULL
    );

    if (!hWnd) {
        MessageBox(NULL, _T("Call to CreateWindowEx failed!"), szTitle, MB_ICONERROR);
        return 1;
    }

    s_icon = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_ICON));
    s_iconDisabled = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_ICON_DISABLED));

    // Initialize NOTIFYICONDATA structure
    s_nid.cbSize = sizeof(NOTIFYICONDATA);
    s_nid.hWnd = hWnd;
    s_nid.uID = 1;
    s_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    s_nid.uCallbackMessage = WM_NMICON;
    s_nid.hIcon = s_icon;
    _tcscpy_s(s_nid.szTip, szTitle);

    // Add the tray icon
    Shell_NotifyIcon(NIM_ADD, &s_nid);

    // Start directory watcher
    for (auto* watcher : s_watcher) {
        if (watcher) {
            watcher->Start();
        }
    }

    // Main message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Stop directory watcher
    for (auto* watcher : s_watcher) {
        if (watcher) {
            watcher->Stop();
            free(watcher);
        }
    }

    for (auto* ext : s_extensions) {
        if (ext) free(ext);
    }

    // Clean up and remove the tray icon
    Shell_NotifyIcon(NIM_DELETE, &s_nid);
    UnregisterClass(wcex.lpszClassName, wcex.hInstance);

    // Release the mutex before exiting
    if (s_mutex)
    {
        ReleaseMutex(s_mutex);
        CloseHandle(s_mutex);
    }

    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WndProc(
    _In_ HWND   hWnd,
    _In_ UINT   message,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
) {
    switch (message) {
    case WM_CREATE:
        s_uTaskbarRestart = RegisterWindowMessage(_T("TaskbarCreated"));
        break;

    // Tray icon message
    case WM_NMICON:
        if (LOWORD(lParam) == WM_RBUTTONUP) OpenPopup(hWnd);
        else if (LOWORD(lParam) == WM_LBUTTONUP) Toggle();
        break;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        if (message == s_uTaskbarRestart)
        {
            Shell_NotifyIcon(NIM_ADD, &s_nid);
            break;
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

VOID OpenPopup(HWND hWnd)
{
    // Initialize Popup menu
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, 4, s_disabled ? _T("Enable") : _T("Disable"));
    AppendMenu(hMenu, MF_SEPARATOR, NULL, NULL);
    AppendMenu(hMenu, MF_STRING, 2, _T("Settings"));
    AppendMenu(hMenu, MF_STRING, 3, _T("About..."));
    AppendMenu(hMenu, MF_SEPARATOR, NULL, NULL);
    AppendMenu(hMenu, MF_STRING, 1, _T("Exit"));

    POINT pt;
    GetCursorPos(&pt);

    UINT cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hWnd, NULL);
    switch (cmd) {
    case 1: // Exit
        PostMessage(hWnd, WM_CLOSE, 0, 0);
        break;

    case 2: { // Settings
        DWORD fileAttributes = GetFileAttributes(s_settingsFile);
        if ((fileAttributes == INVALID_FILE_ATTRIBUTES)
            || (fileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            int result = MessageBox(NULL,
                _T("The settings file does not exist. Do you want to create it?"),
                szTitle,
                MB_YESNO | MB_ICONQUESTION);
            if (result == IDYES) {
                InitSettings(true);
            } else {
                break;
            }
        }
        ShellExecute(NULL, _T("open"), s_settingsFile, NULL, NULL, SW_SHOWNORMAL);
        break;
    }
    case 3: // About
        ShellExecute(NULL, _T("open"), _T("https://www.github.com/Fabi019/AutoUnpack"), NULL, NULL, SW_SHOWNORMAL);
        break;

    case 4: // Toggle
        Toggle();
        break;
    }

    DestroyMenu(hMenu);
}

VOID InitSettings(BOOL createNew) {
    GetModuleFileName(NULL, s_settingsFile, MAX_PATH);
    _tcscpy_s(_tcsrchr(s_settingsFile, _T('\\')) + 1, MAX_PATH - _tcslen(s_settingsFile), szSettingsFile);

    if (createNew)
    {
        WritePrivateProfileString(szCategory, _T("Folders"), szDefaultFolders, s_settingsFile);
        WritePrivateProfileString(szCategory, _T("Extensions"), szDefaultExtensions, s_settingsFile);
        WritePrivateProfileString(szCategory, _T("WaitTimeMs"), szDefaultDelay, s_settingsFile);
        WritePrivateProfileString(szCategory, _T("MaxFileSizeMB"), szDefaultMaxFileSize, s_settingsFile);
        WritePrivateProfileString(szCategory, _T("ZipExe"), szDefaultZipExe, s_settingsFile);
        WritePrivateProfileString(szCategory, _T("DeleteAfter"), _T("0"), s_settingsFile);
        WritePrivateProfileString(szCategory, _T("AutoStart"), _T("0"), s_settingsFile);
        WritePrivateProfileString(szCategory, _T("EfficiencyMode"), _T("1"), s_settingsFile);
        return;
    }

    TCHAR szValue[512];

    GetPrivateProfileString(szCategory, _T("WaitTimeMs"), szDefaultDelay, szValue, 8, s_settingsFile);
    s_waitMs = max(0, _tstoi(szValue));
    GetPrivateProfileString(szCategory, _T("MaxFileSizeMB"), szDefaultMaxFileSize, szValue, 8, s_settingsFile);
    s_maxFileSizeMB = max(0, _tstoi(szValue));
    GetPrivateProfileString(szCategory, _T("ZipExe"), szDefaultZipExe, szValue, MAX_PATH, s_settingsFile);
    _tcscpy_s(s_zipExe, MAX_PATH, szValue);
    GetPrivateProfileString(szCategory, _T("DeleteAfter"), _T("0"), szValue, 2, s_settingsFile);
    s_deleteAfter = !!_tstoi(szValue);
    GetPrivateProfileString(szCategory, _T("AutoStart"), _T("0"), szValue, 2, s_settingsFile);
    s_autoRun = !!_tstoi(szValue);
    GetPrivateProfileString(szCategory, _T("EfficiencyMode"), _T("1"), szValue, 2, s_settingsFile);
    s_efficiencyMode = !!_tstoi(szValue);

    GetPrivateProfileString(szCategory, _T("Folders"), szDefaultFolders, szValue, 512, s_settingsFile);
    int i = 0;
    TCHAR* context = nullptr;
    TCHAR* token = _tcstok_s(szValue, szDelimiter, &context);
    while (token != NULL && i < MAX_DELIM_ENTRIES) {
        s_watcher[i++] = new DirectoryWatcher{ token, (DWORD) s_waitMs, (DWORD) s_maxFileSizeMB, &FileCallback};
        token = _tcstok_s(NULL, szDelimiter, &context);
    }

    GetPrivateProfileString(szCategory, _T("Extensions"), szDefaultExtensions, szValue, 512, szSettingsFile);
    i = 0;
    context = nullptr;
    token = _tcstok_s(szValue, szDelimiter, &context);
    while (token != NULL && i < MAX_DELIM_ENTRIES) {
        auto len = _tcslen(token) + 1;
        s_extensions[i] = new TCHAR[len];
        _tcscpy_s(s_extensions[i++], len, token);
        token = _tcstok_s(NULL, szDelimiter, &context);
    }
}

VOID SetEfficiencyMode(BOOL enable) {
    if (!enable) return;

    // Set process priority to idle
    SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);

    // Throttle execution speed (EcoQos)
    PROCESS_POWER_THROTTLING_STATE pic;
    pic.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    pic.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    pic.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;

    typedef BOOL(WINAPI* PFN_SetProcessInformation)(
        HANDLE hProcess,
        PROCESS_INFORMATION_CLASS ProcessInformationClass,
        LPVOID ProcessInformation,
        DWORD ProcessInformationSize
    );

    // Try retrieving the function
    auto pSetProcessInformation =
        (PFN_SetProcessInformation)GetProcAddress(
            GetModuleHandleA("kernel32.dll"), "SetProcessInformation");

    if (pSetProcessInformation)
    {
        pSetProcessInformation(GetCurrentProcess(),
            ProcessPowerThrottling,
            &pic,
            sizeof(PROCESS_POWER_THROTTLING_STATE));
    }
}

VOID SetStartup(BOOL enable)
{
    HKEY hKey;
    RegOpenKeyEx(HKEY_CURRENT_USER, _T("Software\\Microsoft\\Windows\\CurrentVersion\\Run"), 0, KEY_SET_VALUE, &hKey);

    if (enable) {
        TCHAR szPath[MAX_PATH];
        GetModuleFileName(NULL, szPath, MAX_PATH);
        RegSetValueEx(hKey, szTitle, 0, REG_SZ, (BYTE*)szPath, (DWORD)(_tcslen(szPath) + 1) * sizeof(TCHAR));
    } else {
        RegDeleteValue(hKey, szTitle);
    }

    RegCloseKey(hKey);
}

VOID FileCallback(const TCHAR* folder, const TCHAR* fileName, const TCHAR* fullPath)
{
    if (s_disabled) return;

    DbgPrint(_T("File: %s\n"), fullPath);

    const auto fileLen = lstrlen(fileName);
    for (auto* ext : s_extensions)
    {
        if (!ext) continue;

        const auto extLen = lstrlen(ext);
        if (fileLen >= extLen && _tcsncmp(fileName + (fileLen - extLen), ext, extLen) == 0)
        {
            DbgPrint(_T("Found match %s\n"), ext);
            UnpackFile(folder, fullPath);
        }
    }
}

BOOL UnpackFile(const TCHAR* folder, const TCHAR* fullPath)
{
    // Unpack command
    TCHAR cmd[512];
    lstrcpyn(cmd, s_zipExe, MAX_PATH);
    lstrcat(cmd, _T(" x \""));
    lstrcat(cmd, fullPath);
    lstrcat(cmd, _T("\" -spe -y -o*"));

    DbgPrint(_T("Command: %s\n"), cmd);

    // Setup process startup info
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    // Create process
    BOOL result = CreateProcess(
        NULL,                           // lpApplicationName
        cmd,                            // lpCommandLine
        NULL, NULL,                     // process/thread security
        FALSE,                          // inherit handles
        CREATE_NO_WINDOW,               // flags
        NULL, folder,                   // environment & cwd
        &si, &pi
    );

    if (result)
    {
        // Wait until the extraction is complete
        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD ec;
        GetExitCodeProcess(pi.hProcess, &ec);
        if (ec != 0)
        {
            MessageBox(NULL, _T("Failed to unpack archive."), szTitle, MB_ICONERROR | MB_OK);
        }
        else if (s_deleteAfter)
        {
            DeleteFile(fullPath);
        }

        // Cleanup handles
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else
    {
        MessageBox(NULL, _T("Failed to create process."), szTitle, MB_ICONERROR | MB_OK);
        return 1;
    }

    return 0;
}

VOID Toggle()
{
    s_disabled = !s_disabled;
    s_nid.hIcon = s_disabled ? s_iconDisabled : s_icon;
    Shell_NotifyIcon(NIM_MODIFY, &s_nid);
}