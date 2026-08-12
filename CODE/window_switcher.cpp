#include "window_switcher.h"

#include <dwmapi.h>

#include "resource.h"

#include <string>
#include <vector>

namespace {
constexpr wchar_t kSwitcherClassName[] = L"HagenwareWindowSwitcher";
constexpr int kPadding = 12;
constexpr int kGap = 10;
constexpr int kCardWidth = 210;
constexpr int kCardHeight = 150;
constexpr int kPreviewInset = 8;
constexpr int kPreviewHeight = 108;
constexpr int kMonitorMargin = 40;
constexpr int kSelectionThickness = 3;
constexpr int kGlamourWidth = 480;
constexpr int kGlamourHeight = 180;

struct WindowEntry {
    HWND window = nullptr;
    std::wstring title;
    HTHUMBNAIL thumbnail = nullptr;
};

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
std::vector<WindowEntry> g_entries;
int g_selected = 0;
int g_firstVisible = 0;
int g_visibleCount = 0;
bool g_hiding = false;
HBITMAP g_glamour = nullptr;
int g_contentWidth = 0;

bool IsCandidateWindow(HWND window) {
    if (window == nullptr || window == g_window || window == GetShellWindow() || IsWindowVisible(window) == FALSE) {
        return false;
    }

    const LONG_PTR ex_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((ex_style & WS_EX_TOOLWINDOW) != 0) {
        return false;
    }

    if (GetWindow(window, GW_OWNER) != nullptr && (ex_style & WS_EX_APPWINDOW) == 0) {
        return false;
    }

    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0) {
        return false;
    }

    return GetWindowTextLengthW(window) > 0;
}

BOOL CALLBACK EnumerateWindow(HWND window, LPARAM context) {
    if (!IsCandidateWindow(window)) {
        return TRUE;
    }

    const int title_length = GetWindowTextLengthW(window);
    std::wstring title(static_cast<size_t>(title_length) + 1, L'\0');
    const int copied = GetWindowTextW(window, title.data(), title_length + 1);
    if (copied <= 0) {
        return TRUE;
    }

    title.resize(static_cast<size_t>(copied));
    auto* entries = reinterpret_cast<std::vector<WindowEntry>*>(context);
    entries->push_back(WindowEntry{window, title, nullptr});
    return TRUE;
}

void ClearThumbnails() {
    for (WindowEntry& entry : g_entries) {
        if (entry.thumbnail != nullptr) {
            DwmUnregisterThumbnail(entry.thumbnail);
            entry.thumbnail = nullptr;
        }
    }
}

void ClearEntries() {
    ClearThumbnails();
    g_entries.clear();
}

void RefreshEntries() {
    ClearEntries();
    EnumWindows(EnumerateWindow, reinterpret_cast<LPARAM>(&g_entries));
}

RECT CardRect(int slot) {
    const int cards_width = (g_visibleCount * kCardWidth) + ((g_visibleCount - 1) * kGap);
    const LONG left = static_cast<LONG>((g_contentWidth - cards_width) / 2 + slot * (kCardWidth + kGap));
    const LONG top = kPadding + kGlamourHeight + kGap;
    return RECT{left, top, left + kCardWidth, top + kCardHeight};
}

RECT PreviewBounds(int slot) {
    RECT card = CardRect(slot);
    return RECT{
        card.left + kPreviewInset,
        card.top + kPreviewInset,
        card.right - kPreviewInset,
        card.top + kPreviewInset + kPreviewHeight};
}

RECT FitThumbnail(const RECT& bounds, const SIZE& source) {
    const LONG available_width = bounds.right - bounds.left;
    const LONG available_height = bounds.bottom - bounds.top;
    if (source.cx <= 0 || source.cy <= 0 || available_width <= 0 || available_height <= 0) {
        return bounds;
    }

    LONG width = available_width;
    LONG height = static_cast<LONG>(
        (static_cast<long long>(source.cy) * available_width) / source.cx);

    if (height > available_height) {
        height = available_height;
        width = static_cast<LONG>(
            (static_cast<long long>(source.cx) * available_height) / source.cy);
    }

    const LONG left = bounds.left + (available_width - width) / 2;
    const LONG top = bounds.top + (available_height - height) / 2;
    return RECT{left, top, left + width, top + height};
}

void EnsureSelectedVisible() {
    const int count = static_cast<int>(g_entries.size());
    if (count == 0 || g_visibleCount <= 0) {
        g_firstVisible = 0;
        return;
    }

    if (g_selected < g_firstVisible) {
        g_firstVisible = g_selected;
    } else if (g_selected >= g_firstVisible + g_visibleCount) {
        g_firstVisible = g_selected - g_visibleCount + 1;
    }

    const int max_first = count > g_visibleCount ? count - g_visibleCount : 0;
    if (g_firstVisible > max_first) {
        g_firstVisible = max_first;
    }
    if (g_firstVisible < 0) {
        g_firstVisible = 0;
    }
}

void UpdateThumbnailLayout() {
    for (size_t i = 0; i < g_entries.size(); ++i) {
        WindowEntry& entry = g_entries[i];
        if (entry.thumbnail == nullptr) {
            continue;
        }

        const int index = static_cast<int>(i);
        const bool visible = index >= g_firstVisible && index < g_firstVisible + g_visibleCount;
        DWM_THUMBNAIL_PROPERTIES properties{};

        if (!visible) {
            properties.dwFlags = DWM_TNP_VISIBLE;
            properties.fVisible = FALSE;
            DwmUpdateThumbnailProperties(entry.thumbnail, &properties);
            continue;
        }

        SIZE source_size{};
        DwmQueryThumbnailSourceSize(entry.thumbnail, &source_size);
        const RECT destination = FitThumbnail(PreviewBounds(index - g_firstVisible), source_size);

        properties.dwFlags = DWM_TNP_RECTDESTINATION |
            DWM_TNP_VISIBLE |
            DWM_TNP_OPACITY |
            DWM_TNP_SOURCECLIENTAREAONLY;
        properties.rcDestination = destination;
        properties.opacity = 255;
        properties.fVisible = TRUE;
        properties.fSourceClientAreaOnly = FALSE;
        DwmUpdateThumbnailProperties(entry.thumbnail, &properties);
    }
}

void RegisterThumbnails() {
    ClearThumbnails();
    for (WindowEntry& entry : g_entries) {
        HTHUMBNAIL thumbnail = nullptr;
        if (SUCCEEDED(DwmRegisterThumbnail(g_window, entry.window, &thumbnail))) {
            entry.thumbnail = thumbnail;
        }
    }
}

void MoveSelection(int direction) {
    const int count = static_cast<int>(g_entries.size());
    if (count == 0) {
        return;
    }

    if (direction < 0) {
        g_selected = g_selected == 0 ? count - 1 : g_selected - 1;
    } else {
        g_selected = g_selected + 1 == count ? 0 : g_selected + 1;
    }

    EnsureSelectedVisible();
    UpdateThumbnailLayout();
    InvalidateRect(g_window, nullptr, FALSE);
}

void FocusSwitcher() {
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

void ActivateWindow(HWND target) {
    if (target == nullptr || IsWindow(target) == FALSE) {
        return;
    }

    const DWORD current_thread = GetCurrentThreadId();
    const DWORD target_thread = GetWindowThreadProcessId(target, nullptr);
    BOOL attached = FALSE;

    if (target_thread != 0 && target_thread != current_thread) {
        attached = AttachThreadInput(current_thread, target_thread, TRUE);
    }

    if (IsIconic(target) != FALSE) {
        ShowWindow(target, SW_RESTORE);
    } else {
        ShowWindow(target, SW_SHOW);
    }

    BringWindowToTop(target);
    SetForegroundWindow(target);

    if (attached != FALSE) {
        AttachThreadInput(current_thread, target_thread, FALSE);
    }
}

void ActivateSelected() {
    if (g_selected < 0 || g_selected >= static_cast<int>(g_entries.size())) {
        return;
    }

    HWND target = g_entries[static_cast<size_t>(g_selected)].window;
    WindowSwitcher::Hide();
    ActivateWindow(target);
}

void PaintSwitcher(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);

    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    FrameRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    HGDIOBJ previous_font = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));

    if (g_glamour != nullptr) {
        HDC image_dc = CreateCompatibleDC(dc);
        HGDIOBJ previous_image = SelectObject(image_dc, g_glamour);
        const int left = (g_contentWidth - kGlamourWidth) / 2;
        StretchBlt(dc, left, kPadding, kGlamourWidth, kGlamourHeight, image_dc, 0, 0, 4096, 1536, SRCCOPY);
        SelectObject(image_dc, previous_image);
        DeleteDC(image_dc);
    }

    for (int slot = 0; slot < g_visibleCount; ++slot) {
        const int index = g_firstVisible + slot;
        if (index >= static_cast<int>(g_entries.size())) {
            break;
        }

        RECT card = CardRect(slot);
        FrameRect(dc, &card, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        if (index == g_selected) {
            RECT selection = card;
            for (int line = 1; line < kSelectionThickness; ++line) {
                InflateRect(&selection, -1, -1);
                FrameRect(dc, &selection, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            }
        }

        RECT title_rect{
            card.left + kPreviewInset,
            card.top + kPreviewInset + kPreviewHeight + 4,
            card.right - kPreviewInset,
            card.bottom - 4};

        DrawTextW(
            dc,
            g_entries[static_cast<size_t>(index)].title.c_str(),
            -1,
            &title_rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    if (previous_font != nullptr) {
        SelectObject(dc, previous_font);
    }

    EndPaint(window, &paint);
}

void PositionSwitcher(HWND anchor) {
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);

    RECT work_area{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    HMONITOR monitor = MonitorFromWindow(
        anchor != nullptr ? anchor : GetDesktopWindow(),
        MONITOR_DEFAULTTONEAREST);

    if (GetMonitorInfoW(monitor, &monitor_info) != FALSE) {
        work_area = monitor_info.rcWork;
    }

    const int work_width = static_cast<int>(work_area.right - work_area.left);
    const int work_height = static_cast<int>(work_area.bottom - work_area.top);
    int max_visible = (
        work_width - (2 * kMonitorMargin) - (2 * kPadding) + kGap) /
        (kCardWidth + kGap);
    if (max_visible < 1) {
        max_visible = 1;
    }

    const int entry_count = static_cast<int>(g_entries.size());
    g_visibleCount = entry_count < max_visible ? entry_count : max_visible;

    const int cards_width = (g_visibleCount * kCardWidth) + ((g_visibleCount - 1) * kGap);
    g_contentWidth = cards_width > kGlamourWidth ? cards_width : kGlamourWidth;
    const int window_width = (2 * kPadding) + g_contentWidth;
    const int window_height = (2 * kPadding) + kGlamourHeight + kGap + kCardHeight;
    const int x = work_area.left + (work_width - window_width) / 2;
    const int y = work_area.top + (work_height - window_height) / 2;

    SetWindowPos(
        g_window,
        HWND_TOPMOST,
        x,
        y,
        window_width,
        window_height,
        SWP_SHOWWINDOW);
}

LRESULT CALLBACK SwitcherWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT:
        PaintSwitcher(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN:
        switch (wparam) {
        case VK_LEFT:
        case VK_UP:
            MoveSelection(-1);
            return 0;
        case VK_RIGHT:
        case VK_DOWN:
            MoveSelection(1);
            return 0;
        case VK_SPACE:
            ActivateSelected();
            return 0;
        case VK_ESCAPE:
            WindowSwitcher::Hide();
            return 0;
        default:
            break;
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE && !g_hiding && IsWindowVisible(window) != FALSE) {
            WindowSwitcher::Hide();
        }
        return 0;
    case WM_CLOSE:
        WindowSwitcher::Hide();
        return 0;
    case WM_DESTROY:
        ClearEntries();
        return 0;
    default:
        break;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace

namespace WindowSwitcher {

bool Initialize(HINSTANCE instance) {
    g_instance = instance;

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = SwitcherWindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    window_class.lpszClassName = kSwitcherClassName;

    if (RegisterClassW(&window_class) == 0) {
        g_instance = nullptr;
        return false;
    }

    g_window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kSwitcherClassName,
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
        UnregisterClassW(kSwitcherClassName, instance);
        g_instance = nullptr;
        return false;
    }

    g_glamour = LoadBitmapW(instance, MAKEINTRESOURCEW(IDB_SWITCHER_GLAMOUR));
    if (g_glamour == nullptr) {
        DestroyWindow(g_window);
        g_window = nullptr;
        UnregisterClassW(kSwitcherClassName, instance);
        g_instance = nullptr;
        return false;
    }

    return true;
}

void Show() {
    if (g_window == nullptr || IsWindowVisible(g_window) != FALSE) {
        return;
    }

    HWND foreground = GetForegroundWindow();
    RefreshEntries();
    if (g_entries.empty()) {
        return;
    }

    g_selected = 0;
    g_firstVisible = 0;
    for (size_t i = 0; i < g_entries.size(); ++i) {
        if (g_entries[i].window == foreground) {
            g_selected = static_cast<int>(i);
            break;
        }
    }

    PositionSwitcher(foreground);
    EnsureSelectedVisible();
    UpdateWindow(g_window);
    RegisterThumbnails();
    UpdateThumbnailLayout();
    InvalidateRect(g_window, nullptr, FALSE);
    FocusSwitcher();
}

void Hide() {
    if (g_window == nullptr || g_hiding) {
        return;
    }

    g_hiding = true;
    if (IsWindowVisible(g_window) != FALSE) {
        ShowWindow(g_window, SW_HIDE);
    }
    ClearEntries();
    g_selected = 0;
    g_firstVisible = 0;
    g_visibleCount = 0;
    g_hiding = false;
}

void Shutdown() {
    Hide();

    if (g_window != nullptr) {
        HWND window = g_window;
        g_window = nullptr;
        DestroyWindow(window);
    }

    if (g_instance != nullptr) {
        UnregisterClassW(kSwitcherClassName, g_instance);
        g_instance = nullptr;
    }

    if (g_glamour != nullptr) {
        DeleteObject(g_glamour);
        g_glamour = nullptr;
    }
}

} // namespace WindowSwitcher
