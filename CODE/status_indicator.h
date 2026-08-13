#pragma once

#include <windows.h>

namespace StatusIndicator {

bool Initialize(HINSTANCE instance);
void Show();
void Hide();
void Shutdown();

} // namespace StatusIndicator
