#pragma once

#include <windows.h>

namespace ScannerWindow {

bool Initialize(HINSTANCE instance);
bool Toggle(HWND anchor_window);
void Shutdown();

} // namespace ScannerWindow
