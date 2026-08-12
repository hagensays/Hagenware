#pragma once

#include <windows.h>

namespace Screenshot {

bool Initialize();
void ShowIndicatorForDeck(HWND deck_window);
void HideIndicator();
void RequestCapture();
void Shutdown();

} // namespace Screenshot
