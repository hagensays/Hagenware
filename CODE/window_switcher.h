#pragma once

#include <windows.h>

namespace WindowSwitcher {

bool Initialize(HINSTANCE instance);
void Show();
void Hide();
void Shutdown();

} // namespace WindowSwitcher
