#pragma once

#include <windows.h>

namespace ScannerResultControls {
bool Initialize(HINSTANCE instance, HWND parent, HFONT font);
bool HandleCommand(int control_id, int notification);
void Paint(HDC dc, const RECT& client);
void RefreshOnShow();
void Shutdown();
} // namespace ScannerResultControls
