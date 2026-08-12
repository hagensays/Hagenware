#include "input_dismiss.h"

#include "window_placement.h"
#include "window_switcher.h"

namespace {
constexpr wchar_t kSwitcherClassName[] = L"HagenwareWindowSwitcher";
constexpr wchar_t kPlacementClassName[] = L"HagenwareWindowPlacement";

enum class SurfaceKind {
    None,
    Switcher,
    Placement,
};

HWINEVENTHOOK g_windowEventHook = nullptr;
HHOOK g_keyboardHook = nullptr;
HHOOK g_mouseHook = nullptr;
HWND g_surfaceWindow = nullptr;
SurfaceKind g_surfaceKind = SurfaceKind::None;
InputDismiss::SuppressModifierCallback g_suppressModifier = nullptr;

bool IsShiftKey(DWORD virtual_key) {
    return virtual_key == VK_SHIFT || virtual_key == VK_LSHIFT || virtual_key == VK_RSHIFT;
}

bool IsControlKey(DWORD virtual_key) {
    return virtual_key == VK_CONTROL || virtual_key == VK_LCONTROL || virtual_key == VK_RCONTROL;
}

bool IsProgrammedKey(SurfaceKind surface, DWORD virtual_key) {
    if (surface == SurfaceKind::Switcher) {
        return virtual_key == VK_LEFT ||
            virtual_key == VK_UP ||
            virtual_key == VK_RIGHT ||
            virtual_key == VK_DOWN ||
            virtual_key == VK_SPACE ||
            virtual_key == VK_ESCAPE;
    }

    if (surface == SurfaceKind::Placement) {
        return virtual_key >= VK_NUMPAD1 && virtual_key <= VK_NUMPAD9;
    }

    return false;
}

bool IsOwnTriggerKey(SurfaceKind surface, DWORD virtual_key) {
    if (surface == SurfaceKind::Switcher) {
        return IsShiftKey(virtual_key);
    }
    if (surface == SurfaceKind::Placement) {
        return IsControlKey(virtual_key);
    }
    return false;
}

bool IsDismissMouseInput(WPARAM message) {
    return message == WM_LBUTTONDOWN ||
        message == WM_RBUTTONDOWN ||
        message == WM_MBUTTONDOWN ||
        message == WM_XBUTTONDOWN ||
        message == WM_MOUSEWHEEL ||
        message == WM_MOUSEHWHEEL;
}

SurfaceKind SurfaceForWindow(HWND window) {
    if (window == nullptr) {
        return SurfaceKind::None;
    }

    wchar_t class_name[64]{};
    const int capacity = static_cast<int>(sizeof(class_name) / sizeof(class_name[0]));
    if (GetClassNameW(window, class_name, capacity) <= 0) {
        return SurfaceKind::None;
    }

    if (lstrcmpW(class_name, kSwitcherClassName) == 0) {
        return SurfaceKind::Switcher;
    }
    if (lstrcmpW(class_name, kPlacementClassName) == 0) {
        return SurfaceKind::Placement;
    }
    return SurfaceKind::None;
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

    g_surfaceWindow = nullptr;
    g_surfaceKind = SurfaceKind::None;
}

void DismissSurfaceForPassThrough() {
    if (g_surfaceKind == SurfaceKind::Switcher) {
        WindowSwitcher::DismissForPassThrough();
    } else if (g_surfaceKind == SurfaceKind::Placement) {
        WindowPlacement::DismissForPassThrough();
    }
}

LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION &&
        g_surfaceWindow != nullptr &&
        IsWindowVisible(g_surfaceWindow) != FALSE &&
        (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN)) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);

        if (!IsProgrammedKey(g_surfaceKind, key->vkCode)) {
            if (IsOwnTriggerKey(g_surfaceKind, key->vkCode) && g_suppressModifier != nullptr) {
                g_suppressModifier(key->vkCode);
            }

            DismissSurfaceForPassThrough();
        }
    }

    return CallNextHookEx(nullptr, code, wparam, lparam);
}

LRESULT CALLBACK MouseHookProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION &&
        g_surfaceWindow != nullptr &&
        IsWindowVisible(g_surfaceWindow) != FALSE &&
        IsDismissMouseInput(wparam)) {
        DismissSurfaceForPassThrough();
    }

    return CallNextHookEx(nullptr, code, wparam, lparam);
}

void StartInputHooks(HWND surface_window, SurfaceKind surface_kind) {
    StopInputHooks();
    g_surfaceWindow = surface_window;
    g_surfaceKind = surface_kind;

    g_keyboardHook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        KeyboardHookProc,
        GetModuleHandleW(nullptr),
        0);

    if (g_keyboardHook == nullptr) {
        PostMessageW(surface_window, WM_CLOSE, 0, 0);
        g_surfaceWindow = nullptr;
        g_surfaceKind = SurfaceKind::None;
        return;
    }

    g_mouseHook = SetWindowsHookExW(
        WH_MOUSE_LL,
        MouseHookProc,
        GetModuleHandleW(nullptr),
        0);

    if (g_mouseHook == nullptr) {
        PostMessageW(surface_window, WM_CLOSE, 0, 0);
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
    if (object_id != OBJID_WINDOW || child_id != CHILDID_SELF) {
        return;
    }

    const SurfaceKind surface = SurfaceForWindow(window);
    if (surface == SurfaceKind::None) {
        return;
    }

    if (event == EVENT_OBJECT_SHOW) {
        StartInputHooks(window, surface);
    } else if (event == EVENT_OBJECT_HIDE && window == g_surfaceWindow) {
        StopInputHooks();
    }
}
} // namespace

namespace InputDismiss {

bool Start(SuppressModifierCallback suppress_modifier_until_release) {
    Stop();

    if (suppress_modifier_until_release == nullptr) {
        return false;
    }

    g_suppressModifier = suppress_modifier_until_release;
    g_windowEventHook = SetWinEventHook(
        EVENT_OBJECT_SHOW,
        EVENT_OBJECT_HIDE,
        nullptr,
        WindowEventProc,
        GetCurrentProcessId(),
        0,
        WINEVENT_OUTOFCONTEXT);

    if (g_windowEventHook == nullptr) {
        g_suppressModifier = nullptr;
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

    g_suppressModifier = nullptr;
}

} // namespace InputDismiss
