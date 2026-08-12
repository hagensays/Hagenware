#include "input_dismiss.h"

#include <windows.h>

namespace {
constexpr wchar_t kSwitcherClassName[] = L"HagenwareWindowSwitcher";

HWINEVENTHOOK g_windowEventHook = nullptr;
HHOOK g_keyboardHook = nullptr;
HHOOK g_mouseHook = nullptr;
HWND g_switcherWindow = nullptr;
InputDismiss::TriggerEnabledCallback g_setTriggerEnabled = nullptr;

bool IsProgrammedKey(DWORD virtual_key) {
    return virtual_key == VK_LEFT ||
        virtual_key == VK_UP ||
        virtual_key == VK_RIGHT ||
        virtual_key == VK_DOWN ||
        virtual_key == VK_SPACE;
}

bool IsDismissMouseInput(WPARAM message) {
    return message == WM_LBUTTONDOWN ||
        message == WM_RBUTTONDOWN ||
        message == WM_MBUTTONDOWN ||
        message == WM_XBUTTONDOWN ||
        message == WM_MOUSEWHEEL ||
        message == WM_MOUSEHWHEEL;
}

bool IsSwitcherWindow(HWND window) {
    wchar_t class_name[64]{};
    const int capacity = static_cast<int>(sizeof(class_name) / sizeof(class_name[0]));
    return window != nullptr &&
        GetClassNameW(window, class_name, capacity) > 0 &&
        lstrcmpW(class_name, kSwitcherClassName) == 0;
}

void StopInputHooks() {
    if (g_mouseHook != nullptr) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }

    if (g_keyboardHook != nullptr) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }

    g_switcherWindow = nullptr;
}

LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION &&
        g_switcherWindow != nullptr &&
        IsWindowVisible(g_switcherWindow) != FALSE &&
        (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN)) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);

        if (!IsProgrammedKey(key->vkCode)) {
            g_setTriggerEnabled(false);
            PostMessageW(g_switcherWindow, WM_CLOSE, 0, 0);
            const LRESULT result = CallNextHookEx(g_keyboardHook, code, wparam, lparam);
            g_setTriggerEnabled(true);
            return result;
        }
    }

    return CallNextHookEx(g_keyboardHook, code, wparam, lparam);
}

LRESULT CALLBACK MouseHookProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION &&
        g_switcherWindow != nullptr &&
        IsWindowVisible(g_switcherWindow) != FALSE &&
        IsDismissMouseInput(wparam)) {
        PostMessageW(g_switcherWindow, WM_CLOSE, 0, 0);
    }

    return CallNextHookEx(g_mouseHook, code, wparam, lparam);
}

void StartInputHooks(HWND switcher_window) {
    StopInputHooks();
    g_switcherWindow = switcher_window;

    g_keyboardHook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        KeyboardHookProc,
        GetModuleHandleW(nullptr),
        0);

    if (g_keyboardHook == nullptr) {
        PostMessageW(switcher_window, WM_CLOSE, 0, 0);
        g_switcherWindow = nullptr;
        return;
    }

    g_mouseHook = SetWindowsHookExW(
        WH_MOUSE_LL,
        MouseHookProc,
        GetModuleHandleW(nullptr),
        0);

    if (g_mouseHook == nullptr) {
        PostMessageW(switcher_window, WM_CLOSE, 0, 0);
        StopInputHooks();
    }
}

void CALLBACK WindowEventProc(
    HWINEVENTHOOK,
    DWORD event,
    HWND window,
    LONG object_id,
    LONG child_id,
    DWORD,
    DWORD) {
    if (object_id != OBJID_WINDOW || child_id != CHILDID_SELF || !IsSwitcherWindow(window)) {
        return;
    }

    if (event == EVENT_OBJECT_SHOW) {
        StartInputHooks(window);
    } else if (event == EVENT_OBJECT_HIDE && window == g_switcherWindow) {
        StopInputHooks();
    }
}
} // namespace

namespace InputDismiss {

bool Start(TriggerEnabledCallback set_trigger_enabled) {
    Stop();

    if (set_trigger_enabled == nullptr) {
        return false;
    }

    g_setTriggerEnabled = set_trigger_enabled;
    g_windowEventHook = SetWinEventHook(
        EVENT_OBJECT_SHOW,
        EVENT_OBJECT_HIDE,
        nullptr,
        WindowEventProc,
        GetCurrentProcessId(),
        0,
        WINEVENT_OUTOFCONTEXT);

    if (g_windowEventHook == nullptr) {
        g_setTriggerEnabled = nullptr;
        return false;
    }

    return true;
}

void Stop() {
    StopInputHooks();

    if (g_windowEventHook != nullptr) {
        UnhookWinEvent(g_windowEventHook);
        g_windowEventHook = nullptr;
    }

    g_setTriggerEnabled = nullptr;
}

} // namespace InputDismiss
