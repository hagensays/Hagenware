#include "status_indicator.h"

namespace {
constexpr wchar_t kStatusIndicatorClassName[] = L"HagenwareStatusIndicator";
constexpr int kIndicatorWidth = 80;
constexpr int kIndicatorHeight = 2;
constexpr COLORREF kIndicatorColor = RGB(220, 0, 0);

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
HBRUSH g_brush = nullptr;

void PositionIndicator() {
    if (g_window == nullptr) {
        return;
    }

    RECT monitor_rect{
        0,
        0,
        GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN)};

    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    const HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (monitor != nullptr && GetMonitorInfoW(monitor, &monitor_info) != FALSE) {
        monitor_rect = monitor_info.rcMonitor;
    }

    const int monitor_width = static_cast<int>(monitor_rect.right - monitor_rect.left);
    const int indicator_width = monitor_width < kIndicatorWidth ? monitor_width : kIndicatorWidth;
    const int x = monitor_rect.left + (monitor_width - indicator_width) / 2;
    const int y = monitor_rect.bottom - kIndicatorHeight;

    SetWindowPos(
        g_window,
        HWND_TOPMOST,
        x,
        y,
        indicator_width,
        kIndicatorHeight,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

LRESULT CALLBACK StatusIndicatorWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        FillRect(dc, &client, g_brush);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DISPLAYCHANGE:
        PositionIndicator();
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}
} // namespace

namespace StatusIndicator {

bool Initialize(HINSTANCE instance) {
    if (instance == nullptr || g_window != nullptr) {
        return false;
    }

    g_instance = instance;
    g_brush = CreateSolidBrush(kIndicatorColor);
    if (g_brush == nullptr) {
        g_instance = nullptr;
        return false;
    }

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = StatusIndicatorWindowProc;
    window_class.hInstance = instance;
    window_class.hbrBackground = g_brush;
    window_class.lpszClassName = kStatusIndicatorClassName;

    if (RegisterClassW(&window_class) == 0) {
        DeleteObject(g_brush);
        g_brush = nullptr;
        g_instance = nullptr;
        return false;
    }

    g_window = CreateWindowExW(
        WS_EX_TOPMOST |
            WS_EX_TOOLWINDOW |
            WS_EX_NOACTIVATE |
            WS_EX_TRANSPARENT |
            WS_EX_LAYERED,
        kStatusIndicatorClassName,
        L"",
        WS_POPUP,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (g_window == nullptr) {
        UnregisterClassW(kStatusIndicatorClassName, instance);
        DeleteObject(g_brush);
        g_brush = nullptr;
        g_instance = nullptr;
        return false;
    }

    SetLayeredWindowAttributes(g_window, 0, 255, LWA_ALPHA);
    PositionIndicator();
    return true;
}

void Shutdown() {
    if (g_window != nullptr) {
        HWND window = g_window;
        g_window = nullptr;
        DestroyWindow(window);
    }

    if (g_instance != nullptr) {
        UnregisterClassW(kStatusIndicatorClassName, g_instance);
        g_instance = nullptr;
    }

    if (g_brush != nullptr) {
        DeleteObject(g_brush);
        g_brush = nullptr;
    }
}

} // namespace StatusIndicator
