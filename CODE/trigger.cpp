#include "trigger.h"

namespace {
HHOOK g_hook = nullptr;
HWND g_targetWindow = nullptr;
UINT g_triggerMessage = 0;
bool g_shiftCandidate = false;
DWORD g_shiftScanCode = 0;

bool IsShiftKey(DWORD virtual_key) {
    return virtual_key == VK_SHIFT || virtual_key == VK_LSHIFT || virtual_key == VK_RSHIFT;
}

void CancelCandidate() {
    g_shiftCandidate = false;
    g_shiftScanCode = 0;
}

LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
        const bool key_down = wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;
        const bool key_up = wparam == WM_KEYUP || wparam == WM_SYSKEYUP;

        if (key_down) {
            if (IsShiftKey(key->vkCode)) {
                if (!g_shiftCandidate) {
                    g_shiftCandidate = true;
                    g_shiftScanCode = key->scanCode;
                } else if (key->scanCode != g_shiftScanCode) {
                    CancelCandidate();
                }
            } else {
                CancelCandidate();
            }
        } else if (key_up) {
            if (g_shiftCandidate && IsShiftKey(key->vkCode) && key->scanCode == g_shiftScanCode) {
                CancelCandidate();
                if (g_targetWindow != nullptr && g_triggerMessage != 0) {
                    PostMessageW(g_targetWindow, g_triggerMessage, 0, 0);
                }
            } else if (g_shiftCandidate) {
                CancelCandidate();
            }
        }
    }

    return CallNextHookEx(nullptr, code, wparam, lparam);
}
} // namespace

namespace Trigger {

bool Start(HWND target_window, UINT trigger_message) {
    Stop();

    g_targetWindow = target_window;
    g_triggerMessage = trigger_message;
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandleW(nullptr), 0);

    if (g_hook == nullptr) {
        g_targetWindow = nullptr;
        g_triggerMessage = 0;
        return false;
    }

    return true;
}

void Stop() {
    if (g_hook != nullptr) {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }

    g_targetWindow = nullptr;
    g_triggerMessage = 0;
    CancelCandidate();
}

} // namespace Trigger
