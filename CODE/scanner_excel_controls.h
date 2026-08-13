#pragma once

#include <windows.h>

namespace ScannerExcelControls {
bool Initialize(HINSTANCE instance, HWND parent, HFONT font);
bool HandleCommand(int control_id, int notification);
void Shutdown();
} // namespace ScannerExcelControls
