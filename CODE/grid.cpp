#include "grid.h"

namespace {
constexpr wchar_t kGridClassName[] = L"HagenwareGrid";
constexpr int kPadding = 10;
constexpr int kGap = 6;
constexpr int kCellWidth = 104;
constexpr int kCellHeight = 72;
constexpr int kWindowWidth = (2 * kPadding) + (3 * kCellWidth) + (2 * kGap);
constexpr int kWindowHeight = (2 * kPadding) + (3 * kCellHeight) + (2 * kGap);

struct GridCell {
    int number;
    const wchar_t* label;
};

constexpr GridCell kCells[] = {
    {7, L"7\nTop left"},
    {8, L"8\nTop half"},
    {9, L"9\nTop right"},
    {4, L"4\nLeft half"},
    {5, L"5\nMax / Restore"},
    {6, L"6\nRight half"},
    {1, L"1\nBottom left"},
    {2, L"2\nBottom half"},
    {3, L"3\nBottom right"},
};

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
HWND g_targetWindow = nullptr;
bool g_hiding = false;

bool IsCandidateTarget(HWND window) {
    return window != nullptr &&
        window != g_window &&
        window != GetShellWindow() &&
        IsWindow(window) != FALSE &&
        IsWindowVisible(window) != FALSE;
}

RECT CellRect(int index) {
    const int row = index / 3;
    const int column = index % 3;
    const LONG left = static_cast<LONG>(kPadding + column * (kCellWidth + kGap));
    const LONG top = static_cast<LONG>(kPadding + row * (kCellHeight + kGap));
    return RECT{left, top, left + kCellWidth, top + kCellHeight};
}

void PaintGrid(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);

    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    FrameRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    HGDIOBJ previous_font = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));

    for (int index = 0; index < 9; ++index) {
        RECT cell = CellRect(index);
        FrameRect(dc, &cell, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        RECT text_bounds = cell;
        InflateRect(&text_bounds, -6, -6);

        RECT measured{0, 0, text_bounds.right - text_bounds.left, 0};
        DrawTextW(
            dc,
            kCells[index].label,
            -1,
            &measured,
            DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);

        const LONG text_height = measured.bottom - measured.top;
        const LONG available_height = text_bounds.bottom - text_bounds.top;
        text_bounds.top += (available_height - text_height) / 2;
        text_bounds.bottom = text_bounds.top + text_height;

        DrawTextW(
            dc,
            kCells[index].label,
            -1,
            &text_bounds,
            DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    }

    if (previous_font != nullptr) {
        SelectObject(dc, previous_font);
    }

    EndPaint(window, &paint);
}

RECT GetTargetWorkArea(HWND target) {
    RECT work_area{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);

    HMONITOR monitor = MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(monitor, &monitor_info) != FALSE) {
        work_area = monitor_info.rcWork;
    }

    return work_area;
}

RECT PlacementRect(int number, const RECT& work_area) {
    const LONG middle_x = work_area.left + (work_area.right - work_area.left) / 2;
    const LONG middle_y = work_area.top + (work_area.bottom - work_area.top) / 2;

    switch (number) {
    case 1:
        return RECT{work_area.left, middle_y, middle_x, work_area.bottom};
    case 2:
        return RECT{work_area.left, middle_y, work_area.right, work_area.bottom};
    case 3:
        return RECT{middle_x, middle_y, work_area.right, work_area.bottom};
    case 4:
        return RECT{work_area.left, work_area.top, middle_x, work_area.bottom};
    case 6:
        return RECT{middle_x, work_area.top, work_area.right, work_area.bottom};
    case 7:
        return RECT{work_area.left, work_area.top, middle_x, middle_y};
    case 8:
        return RECT{work_area.left, work_area.top, work_area.right, middle_y};
    case 9:
        return RECT{middle_x, work_area.top, work_area.right, middle_y};
    default:
        return work_area;
    }
}

void ActivateTarget(HWND target) {
    if (target == nullptr || IsWindow(target) == FALSE) {
        return;
    }

    const DWORD current_thread = GetCurrentThreadId();
    const DWORD target_thread = GetWindowThreadProcessId(target, nullptr);
    BOOL attached = FALSE;

    if (target_thread != 0 && target_thread != current_thread) {
        attached = AttachThreadInput(current_thread, target_thread, TRUE);
    }

    BringWindowToTop(target);
    SetForegroundWindow(target);
    SetActiveWindow(target);

    if (attached != FALSE) {
        AttachThreadInput(current_thread, target_thread, FALSE);
    }
}

void FocusGrid() {
    HWND foreground = GetForegroundWindow();
    const DWORD current_thread = GetCurrentThreadId();
    const DWORD foreground_thread = foreground != nullptr
        ? GetWindowThreadProcessId(foreground, nullptr)
        : 0;

    BOOL attached = FALSE;
    if (foreground_thread != 0 && foreground_thread != current_thread) {
        attached = AttachThreadInput(current_thread, foreground_thread, TRUE);
    }

    SetForegroundWindow(g_window);
    SetActiveWindow(g_window);
    SetFocus(g_window);

    if (attached != FALSE) {
        AttachThreadInput(current_thread, foreground_thread, FALSE);
    }
}

void PositionGrid(HWND target) {
    const RECT work_area = GetTargetWorkArea(target);
    const int work_width = static_cast<int>(work_area.right - work_area.left);
    const int work_height = static_cast<int>(work_area.bottom - work_area.top);
    const int x = work_area.left + (work_width - kWindowWidth) / 2;
    const int y = work_area.top + (work_height - kWindowHeight) / 2;

    SetWindowPos(
        g_window,
        HWND_TOPMOST,
        x,
        y,
        kWindowWidth,
        kWindowHeight,
        SWP_SHOWWINDOW);
}

void ApplyPlacement(int number) {
    HWND target = g_targetWindow;
    if (!IsCandidateTarget(target)) {
        Grid::Hide();
        return;
    }

    const RECT work_area = GetTargetWorkArea(target);
    Grid::Hide();

    if (number == 5) {
        if (IsZoomed(target) != FALSE) {
            ShowWindow(target, SW_RESTORE);
        } else {
            ShowWindow(target, SW_MAXIMIZE);
        }
        ActivateTarget(target);
        return;
    }

    if (IsIconic(target) != FALSE || IsZoomed(target) != FALSE) {
        ShowWindow(target, SW_RESTORE);
    }

    const RECT destination = PlacementRect(number, work_area);
    SetWindowPos(
        target,
        nullptr,
        destination.left,
        destination.top,
        static_cast<int>(destination.right - destination.left),
        static_cast<int>(destination.bottom - destination.top),
        SWP_NOZORDER | SWP_NOACTIVATE);
    ActivateTarget(target);
}

void CancelAndReturnToTarget() {
    HWND target = g_targetWindow;
    Grid::Hide();
    ActivateTarget(target);
}

LRESULT CALLBACK GridWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT:
        PaintGrid(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wparam >= VK_NUMPAD1 && wparam <= VK_NUMPAD9) {
            ApplyPlacement(static_cast<int>(wparam - VK_NUMPAD0));
        } else {
            CancelAndReturnToTarget();
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
        CancelAndReturnToTarget();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE && !g_hiding && IsWindowVisible(window) != FALSE) {
            Grid::Hide();
        }
        return 0;
    case WM_CLOSE:
        CancelAndReturnToTarget();
        return 0;
    case WM_DESTROY:
        g_targetWindow = nullptr;
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}
} // namespace

namespace Grid {

bool Initialize(HINSTANCE instance) {
    if (instance == nullptr) {
        return false;
    }

    g_instance = instance;

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = GridWindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    window_class.lpszClassName = kGridClassName;

    if (RegisterClassW(&window_class) == 0) {
        g_instance = nullptr;
        return false;
    }

    g_window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kGridClassName,
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
        UnregisterClassW(kGridClassName, instance);
        g_instance = nullptr;
        return false;
    }

    return true;
}

void Show() {
    if (g_window == nullptr || IsWindowVisible(g_window) != FALSE) {
        return;
    }

    HWND target = GetForegroundWindow();
    if (!IsCandidateTarget(target)) {
        return;
    }

    g_targetWindow = target;
    PositionGrid(target);
    InvalidateRect(g_window, nullptr, FALSE);
    UpdateWindow(g_window);
    FocusGrid();
}

void Hide() {
    if (g_window == nullptr || g_hiding) {
        return;
    }

    g_hiding = true;
    if (IsWindowVisible(g_window) != FALSE) {
        ShowWindow(g_window, SW_HIDE);
    }
    g_targetWindow = nullptr;
    g_hiding = false;
}

void DismissForPassThrough() {
    CancelAndReturnToTarget();
}

void Shutdown() {
    Hide();

    if (g_window != nullptr) {
        HWND window = g_window;
        g_window = nullptr;
        DestroyWindow(window);
    }

    if (g_instance != nullptr) {
        UnregisterClassW(kGridClassName, g_instance);
        g_instance = nullptr;
    }
}

} // namespace Grid
