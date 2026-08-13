#include "scanner_window.h"

#include <windowsx.h>

#include "lifecycle.h"

namespace {
constexpr wchar_t kClassName[] = L"HagenwareScannerWindow";
constexpr int kWindowWidth = 640;
constexpr int kWindowHeight = 420;
constexpr int kTitleBarHeight = 42;
constexpr int kCloseWidth = 42;

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
HFONT g_font = nullptr;
bool g_activityActive = false;

RECT CloseRect(HWND window) {
    RECT client{};
    GetClientRect(window, &client);
    return RECT{client.right - kCloseWidth, 0, client.right, kTitleBarHeight};
}

void EndActivityIfActive() {
    if (g_activityActive) {
        g_activityActive = false;
        Lifecycle::EndActivity();
    }
}

void HideWindow() {
    if (g_window != nullptr && IsWindowVisible(g_window) != FALSE) {
        ShowWindow(g_window, SW_HIDE);
    }
    EndActivityIfActive();
}

void PaintWindow(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);

    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    FrameRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    HGDIOBJ previous_pen = SelectObject(dc, GetStockObject(BLACK_PEN));
    MoveToEx(dc, client.left, kTitleBarHeight, nullptr);
    LineTo(dc, client.right, kTitleBarHeight);
    if (previous_pen != nullptr) {
        SelectObject(dc, previous_pen);
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    HGDIOBJ previous_font = SelectObject(dc, g_font);

    RECT title_rect{14, 0, client.right - kCloseWidth, kTitleBarHeight};
    DrawTextW(
        dc,
        L"Scanner",
        -1,
        &title_rect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    RECT close_rect = CloseRect(window);
    DrawTextW(
        dc,
        L"\x00D7",
        -1,
        &close_rect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (previous_font != nullptr) {
        SelectObject(dc, previous_font);
    }

    EndPaint(window, &paint);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT:
        PaintWindow(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(window, &point);

        RECT close_rect = CloseRect(window);
        if (PtInRect(&close_rect, point) != FALSE) {
            return HTCLIENT;
        }
        if (point.y >= 0 && point.y < kTitleBarHeight) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_LBUTTONDOWN: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        RECT close_rect = CloseRect(window);
        if (PtInRect(&close_rect, point) != FALSE) {
            HideWindow();
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
            HideWindow();
            return 0;
        }
        break;
    case WM_CLOSE:
        HideWindow();
        return 0;
    case WM_DESTROY:
        EndActivityIfActive();
        g_window = nullptr;
        return 0;
    default:
        break;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

bool EnsureWindow() {
    if (g_window != nullptr) {
        return true;
    }

    g_instance = GetModuleHandleW(nullptr);
    if (g_instance == nullptr) {
        return false;
    }

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = g_instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    window_class.lpszClassName = kClassName;

    if (RegisterClassW(&window_class) == 0) {
        g_instance = nullptr;
        return false;
    }

    g_font = CreateFontW(
        -16,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");

    if (g_font == nullptr) {
        UnregisterClassW(kClassName, g_instance);
        g_instance = nullptr;
        return false;
    }

    g_window = CreateWindowExW(
        WS_EX_APPWINDOW,
        kClassName,
        L"Scanner",
        WS_POPUP,
        0,
        0,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        g_instance,
        nullptr);

    if (g_window == nullptr) {
        DeleteObject(g_font);
        g_font = nullptr;
        UnregisterClassW(kClassName, g_instance);
        g_instance = nullptr;
        return false;
    }

    return true;
}

void PositionWindow(HWND anchor_window) {
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);

    RECT work_area{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    HMONITOR monitor = MonitorFromWindow(
        anchor_window != nullptr ? anchor_window : GetDesktopWindow(),
        MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(monitor, &monitor_info) != FALSE) {
        work_area = monitor_info.rcWork;
    }

    const int width = static_cast<int>(work_area.right - work_area.left);
    const int height = static_cast<int>(work_area.bottom - work_area.top);
    const int x = work_area.left + (width - kWindowWidth) / 2;
    const int y = work_area.top + (height - kWindowHeight) / 2;

    SetWindowPos(
        g_window,
        HWND_TOP,
        x,
        y,
        kWindowWidth,
        kWindowHeight,
        SWP_SHOWWINDOW);
}
} // namespace

namespace ScannerWindow {

bool Show(HWND anchor_window) {
    if (!EnsureWindow()) {
        return false;
    }

    if (!g_activityActive) {
        Lifecycle::BeginActivity();
        g_activityActive = true;
    }

    PositionWindow(anchor_window);
    InvalidateRect(g_window, nullptr, FALSE);
    SetForegroundWindow(g_window);
    SetActiveWindow(g_window);
    SetFocus(g_window);
    return true;
}

void Shutdown() {
    HideWindow();

    if (g_window != nullptr) {
        HWND window = g_window;
        g_window = nullptr;
        DestroyWindow(window);
    }

    if (g_font != nullptr) {
        DeleteObject(g_font);
        g_font = nullptr;
    }

    if (g_instance != nullptr) {
        UnregisterClassW(kClassName, g_instance);
        g_instance = nullptr;
    }
}

} // namespace ScannerWindow
