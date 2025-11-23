#pragma once

#include "stdafx.h"

int APIENTRY _tWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPTSTR    lpCmdLine,
    _In_ int       nCmdShow);

LRESULT CALLBACK WndProc(
    _In_ HWND   hWnd,
    _In_ UINT   message,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
);

VOID OpenPopup(HWND hWnd);
VOID InitSettings(BOOL createNew);
VOID SetEfficiencyMode(BOOL enable);
VOID SetStartup(BOOL enable);
VOID FileCallback(const TCHAR* folder, const TCHAR* fileName, const TCHAR* fullPath);
BOOL UnpackFile(const TCHAR* folder, const TCHAR* fullPath);
VOID Toggle();