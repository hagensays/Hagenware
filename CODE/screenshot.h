#pragma once

#include <windows.h>

namespace Screenshot {

bool Initialize();
void ShowIndicatorForDeck(HWND deck_window);
void HideIndicator();
bool IsIndicatorPoint(POINT point);
void Shutdown();

} // namespace Screenshot
