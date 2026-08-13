#include "scanner_window.h"

#include "lifecycle.h"
#include "scanner_excel_controls.h"
#include "scanner_layout.h"
#include "scanner_path_controls.h"
#include "scanner_result_controls.h"

namespace {
constexpr wchar_t kClassName[] = L"HagenwareScannerWindow";

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
HWND g_anchorWindow = nullptr;
HFONT g_titleFont = nullptr;
HFONT g_bodyFont = nullptr;
bool g_activityActive = false;
bool g_positionInitialized = false;

void EndActivityIfActive() {
    if (g_activityActive) {
        g_activityActive = false;
        Lifecycle::EndActivity();
    }
}

void HideWindow(bool restore_host) {
    if (g_window != nullptr && IsWindowVisible(g_window) != FALSE) {
        ShowWindow(g_window, SW_HIDE);
    }
    EndActivityIfActive();

    if (restore_host && g_anchorWindow != nullptr &&
        IsWindow(g_anchorWindow) != FALSE &&
        IsWindowVisible(g_anchorWindow) != FALSE) {
        SetForegroundWindow(g_anchorWindow);
        SetActiveWindow(g_anchorWindow);
        SetFocus(g_anchorWindow);
    }
}

void PaintWindow(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);

    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    FrameRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    HGDIOBJ previous_font = SelectObject(dc, g_titleFont);
    RECT title_rect{
        ScannerLayout::kOuterMargin,
        ScannerLayout::kTitleTop,
        client.right - ScannerLayout::kOuterMargin,
        ScannerLayout::kTitleTop + ScannerLayout::kTitleHeight};
    DrawTextW(dc, L"Scanner", -1, &title_rect,
        DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    if (previous_font != nullptr) {
        SelectObject(dc, previous_font);
    }

    ScannerResultControls::Paint(dc, client);
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
        const LRESULT hit = DefWindowProcW(window, message, wparam, lparam);
        return hit == HTCLIENT ? HTCAPTION : hit;
    }
    case WM_ACTIVATE:
        if (LOWORD(wparam) != WA_INACTIVE) {
            ScannerResultControls::RefreshOnShow();
        }
        break;
    case WM_COMMAND: {
        const int control_id = LOWORD(wparam);
        const int notification = HIWORD(wparam);
        if (ScannerPathControls::HandleCommand(control_id, notification) ||
            ScannerResultControls::HandleCommand(control_id, notification) ||
            ScannerExcelControls::HandleCommand(control_id, notification)) {
            return 0;
        }
        break;
    }
    case WM_SYSKEYDOWN:
        if (wparam == L'S' && (GetKeyState(VK_MENU) & 0x8000) != 0) {
            HideWindow(true);
            return 0;
        }
        break;
    case WM_SYSCHAR:
        if (wparam == L's' || wparam == L'S') {
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
            HideWindow(true);
            return 0;
        }
        break;
    case WM_CLOSE:
        HideWindow(true);
        return 0;
    case WM_DESTROY:
        EndActivityIfActive();
        ScannerExcelControls::Shutdown();
        ScannerResultControls::Shutdown();
        ScannerPathControls::Shutdown();
        g_window = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
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
    const int x = work_area.left + (width - ScannerLayout::kWindowWidth) / 2;
    const int y = work_area.top + (height - ScannerLayout::kWindowHeight) / 2;
    SetWindowPos(g_window, nullptr, x, y,
        ScannerLayout::kWindowWidth, ScannerLayout::kWindowHeight,
        SWP_NOZORDER | SWP_NOACTIVATE);
}
} // namespace

namespace ScannerWindow {

bool Initialize(HINSTANCE instance) {
    if (g_window != nullptr) {
        return true;
    }
    if (instance == nullptr) {
        return false;
    }
    g_instance = instance;

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    window_class.lpszClassName = kClassName;
    if (RegisterClassW(&window_class) == 0) {
        g_instance = nullptr;
        return false;
    }

    g_bodyFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (g_bodyFont == nullptr) {
        UnregisterClassW(kClassName, g_instance);
        g_instance = nullptr;
        return false;
    }

    g_titleFont = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (g_titleFont == nullptr) {
        DeleteObject(g_bodyFont);
        g_bodyFont = nullptr;
        UnregisterClassW(kClassName, g_instance);
        g_instance = nullptr;
        return false;
    }

    g_window = CreateWindowExW(WS_EX_APPWINDOW, kClassName, L"Scanner", WS_POPUP,
        0, 0, ScannerLayout::kWindowWidth, ScannerLayout::kWindowHeight,
        nullptr, nullptr, instance, nullptr);
    if (g_window == nullptr) {
        DeleteObject(g_titleFont);
        DeleteObject(g_bodyFont);
        g_titleFont = nullptr;
        g_bodyFont = nullptr;
        UnregisterClassW(kClassName, g_instance);
        g_instance = nullptr;
        return false;
    }

    if (!ScannerPathControls::Initialize(instance, g_window, g_bodyFont) ||
        !ScannerResultControls::Initialize(instance, g_window, g_bodyFont) ||
        !ScannerExcelControls::Initialize(instance, g_window, g_bodyFont)) {
        HWND window = g_window;
        DestroyWindow(window);
        DeleteObject(g_titleFont);
        DeleteObject(g_bodyFont);
        g_titleFont = nullptr;
        g_bodyFont = nullptr;
        UnregisterClassW(kClassName, g_instance);
        g_instance = nullptr;
        return false;
    }

    return true;
}

bool Toggle(HWND anchor_window) {
    if (g_window == nullptr) {
        return false;
    }
    if (anchor_window != nullptr && IsWindow(anchor_window) != FALSE) {
        g_anchorWindow = anchor_window;
    }
    if (IsWindowVisible(g_window) != FALSE) {
        HideWindow(true);
        return true;
    }

    if (!g_activityActive) {
        Lifecycle::BeginActivity();
        g_activityActive = true;
    }
    ScannerPathControls::RefreshDrives();
    ScannerResultControls::RefreshOnShow();

    if (!g_positionInitialized) {
        PositionWindow(g_anchorWindow);
        g_positionInitialized = true;
    }

    ShowWindow(g_window, SW_SHOWNORMAL);
    InvalidateRect(g_window, nullptr, FALSE);
    SetForegroundWindow(g_window);
    SetActiveWindow(g_window);
    SetFocus(g_window);
    return true;
}

void Shutdown() {
    HideWindow(false);
    if (g_window != nullptr) {
        HWND window = g_window;
        DestroyWindow(window);
    }
    if (g_titleFont != nullptr) {
        DeleteObject(g_titleFont);
        g_titleFont = nullptr;
    }
    if (g_bodyFont != nullptr) {
        DeleteObject(g_bodyFont);
        g_bodyFont = nullptr;
    }
    if (g_instance != nullptr) {
        UnregisterClassW(kClassName, g_instance);
        g_instance = nullptr;
    }
    g_anchorWindow = nullptr;
    g_positionInitialized = false;
}

} // namespace ScannerWindow
