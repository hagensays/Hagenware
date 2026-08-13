#include "input_dismiss.h"

#include "deck.h"
#include "grid.h"
#include "screenshot.h"

namespace {
enum class SurfaceKind {
    None,
    Deck,
    Grid,
};

HINSTANCE g_module = nullptr;
HHOOK g_mouseHook = nullptr;
HWND g_surfaceWindow = nullptr;
SurfaceKind g_surfaceKind = SurfaceKind::None;

bool IsShiftKey(DWORD virtual_key) {
    return virtual_key == VK_SHIFT || virtual_key == VK_LSHIFT || virtual_key == VK_RSHIFT;
}

bool IsControlKey(DWORD virtual_key) {
    return virtual_key == VK_CONTROL || virtual_key == VK_LCONTROL || virtual_key == VK_RCONTROL;
}

bool IsScreenshotKey(DWORD virtual_key) {
    return virtual_key == VK_SNAPSHOT || virtual_key == VK_PRINT;
}

bool IsProgrammedKey(SurfaceKind surface, DWORD virtual_key) {
    if (surface == SurfaceKind::Deck) {
        return virtual_key == VK_LEFT ||
            virtual_key == VK_UP ||
            virtual_key == VK_RIGHT ||
            virtual_key == VK_DOWN ||
            virtual_key == VK_SPACE ||
            virtual_key == VK_ESCAPE ||
            IsScreenshotKey(virtual_key) ||
            (virtual_key >= L'1' && virtual_key <= L'9');
    }

    if (surface == SurfaceKind::Grid) {
        return virtual_key >= VK_NUMPAD1 && virtual_key <= VK_NUMPAD9;
    }

    return false;
}

bool IsOwnTriggerKey(SurfaceKind surface, DWORD virtual_key) {
    if (surface == SurfaceKind::Deck) {
        return IsShiftKey(virtual_key);
    }
    if (surface == SurfaceKind::Grid) {
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

bool IsPointInsideActiveSurface(const POINT& point) {
    if (g_surfaceWindow == nullptr || IsWindowVisible(g_surfaceWindow) == FALSE) {
        return false;
    }

    if (g_surfaceKind == SurfaceKind::Deck && Screenshot::IsIndicatorPoint(point)) {
        return true;
    }

    RECT surface_rect{};
    if (GetWindowRect(g_surfaceWindow, &surface_rect) == FALSE) {
        return false;
    }

    return PtInRect(&surface_rect, point) != FALSE;
}

void StopMouseHook() {
    if (g_mouseHook != nullptr) {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }

    g_surfaceWindow = nullptr;
    g_surfaceKind = SurfaceKind::None;
}

void DismissSurfaceForPassThrough() {
    const SurfaceKind surface = g_surfaceKind;
    if (surface == SurfaceKind::Deck) {
        Deck::DismissForPassThrough();
    } else if (surface == SurfaceKind::Grid) {
        Grid::DismissForPassThrough();
    }
}

LRESULT CALLBACK MouseHookProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION &&
        g_surfaceWindow != nullptr &&
        IsWindowVisible(g_surfaceWindow) != FALSE &&
        IsDismissMouseInput(wparam)) {
        const auto* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);
        if (!IsPointInsideActiveSurface(mouse->pt)) {
            DismissSurfaceForPassThrough();
        }
    }

    return CallNextHookEx(nullptr, code, wparam, lparam);
}

bool ActivateSurface(HWND window, SurfaceKind surface) {
    if (g_module == nullptr || window == nullptr || surface == SurfaceKind::None) {
        return false;
    }

    StopMouseHook();
    g_surfaceWindow = window;
    g_surfaceKind = surface;

    g_mouseHook = SetWindowsHookExW(
        WH_MOUSE_LL,
        MouseHookProc,
        g_module,
        0);

    if (g_mouseHook == nullptr) {
        g_surfaceWindow = nullptr;
        g_surfaceKind = SurfaceKind::None;
        return false;
    }

    return true;
}
} // namespace

namespace InputDismiss {

bool Start() {
    Stop();
    g_module = GetModuleHandleW(nullptr);
    return g_module != nullptr;
}

bool ActivateDeck(HWND window) {
    return ActivateSurface(window, SurfaceKind::Deck);
}

bool ActivateGrid(HWND window) {
    return ActivateSurface(window, SurfaceKind::Grid);
}

void Deactivate(HWND window) {
    if (window != nullptr && window == g_surfaceWindow) {
        StopMouseHook();
    }
}

KeyboardAction HandleKeyboard(DWORD virtual_key, bool key_down, bool key_up) {
    if (g_surfaceWindow == nullptr || g_surfaceKind == SurfaceKind::None) {
        return KeyboardAction::PassThrough;
    }

    if (IsWindowVisible(g_surfaceWindow) == FALSE) {
        StopMouseHook();
        return KeyboardAction::PassThrough;
    }

    if (g_surfaceKind == SurfaceKind::Deck && IsScreenshotKey(virtual_key)) {
        if (key_down) {
            Screenshot::RequestCapture();
        }
        if (key_down || key_up) {
            return KeyboardAction::Consume;
        }
    }

    if (IsProgrammedKey(g_surfaceKind, virtual_key)) {
        return KeyboardAction::PassThrough;
    }

    if (!key_down) {
        return KeyboardAction::PassThrough;
    }

    const bool own_trigger = IsOwnTriggerKey(g_surfaceKind, virtual_key);
    DismissSurfaceForPassThrough();
    return own_trigger ? KeyboardAction::SuppressTrigger : KeyboardAction::PassThrough;
}

void Stop() {
    StopMouseHook();
    g_module = nullptr;
}

} // namespace InputDismiss
