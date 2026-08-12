#pragma once

#include <windows.h>

namespace InputDismiss {

enum class KeyboardAction {
    PassThrough,
    Consume,
    SuppressTrigger,
};

bool Start();
bool ActivateDeck(HWND window);
bool ActivateGrid(HWND window);
void Deactivate(HWND window);
KeyboardAction HandleKeyboard(DWORD virtual_key, bool key_down, bool key_up);
void Stop();

} // namespace InputDismiss
