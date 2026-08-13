#pragma once

#include <windows.h>

#include <string>

namespace ScannerPathControls {

bool Initialize(HINSTANCE instance, HWND parent, HFONT font);
bool HandleCommand(int control_id, int notification);
void RefreshDrives();
std::wstring CurrentScanRoot();
void Shutdown();

} // namespace ScannerPathControls
