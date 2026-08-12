#include "deck.h"

#include <dwmapi.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {
constexpr wchar_t kDeckClassName[] = L"HagenwareDeck";
constexpr int kPadding = 12;
constexpr int kGap = 10;
constexpr int kCardWidth = 210;
constexpr int kCardHeight = 150;
constexpr int kPreviewInset = 8;
constexpr int kPreviewHeight = 108;
constexpr int kMonitorMargin = 40;
constexpr int kSelectionThickness = 3;
constexpr int kMaxNumberedCards = 9;
constexpr BYTE kThumbnailOpacity = 224;

struct WindowEntry {
    HWND window = nullptr;
    std::wstring title;
    HTHUMBNAIL thumbnail = nullptr;
};

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
HWND g_returnWindow = nullptr;
HWINEVENTHOOK g_foregroundEventHook = nullptr;
HFONT g_numberFont = nullptr;
std::vector<HWND> g_recentWindows;
std::vector<WindowEntry> g_entries;
int g_selected = 0;
int g_firstVisible = 0;
int g_visibleCount = 0;
bool g_hiding = false;

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

void RememberWindow(HWND window) {
    if (!IsCandidateWindow(window)) {
        return;
    }

    const auto existing = std::find(g_recentWindows.begin(), g_recentWindows.end(), window);
    if (existing != g_recentWindows.end()) {
        g_recentWindows.erase(existing);
    }

    g_recentWindows.insert(g_recentWindows.begin(), window);
}

void CALLBACK ForegroundEventProc(
    HWINEVENTHOOK,
    DWORD event,
    HWND window,
    LONG,
    LONG,
    DWORD,
    DWORD) {
    if (event == EVENT_SYSTEM_FOREGROUND) {
        RememberWindow(window);
    }
}

BOOL CALLBACK SeedRecentWindow(HWND window, LPARAM) {
    if (IsCandidateWindow(window)) {
        g_recentWindows.push_back(window);
    }
    return TRUE;
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

size_t RecentRank(HWND window) {
    const auto found = std::find(g_recentWindows.begin(), g_recentWindows.end(), window);
    if (found == g_recentWindows.end()) {
        return g_recentWindows.size();
    }
    return static_cast<size_t>(found - g_recentWindows.begin());
}

void RefreshEntries() {
    ClearEntries();

    g_recentWindows.erase(
        std::remove_if(
            g_recentWindows.begin(),
            g_recentWindows.end(),
            [](HWND window) {
                return !IsCandidateWindow(window);
            }),
        g_recentWindows.end());

    EnumWindows(EnumerateWindow, reinterpret_cast<LPARAM>(&g_entries));

    std::stable_sort(
        g_entries.begin(),
        g_entries.end(),
        [](const WindowEntry& left, const WindowEntry& right) {
            return RecentRank(left.window) < RecentRank(right.window);
        });

    for (const WindowEntry& entry : g_entries) {
        if (std::find(g_recentWindows.begin(), g_recentWindows.end(), entry.window) == g_recentWindows.end()) {
            g_recentWindows.push_back(entry.window);
        }
    }
}

RECT CardRect(int slot) {
    const LONG left = static_cast<LONG>(kPadding + slot * (kCardWidth + kGap));
    const LONG top = kPadding;
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
        properties.opacity = kThumbnailOpacity;
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

void FocusDeck() {
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
    Deck::Hide();
    ActivateWindow(target);
}

void ActivateVisibleSlot(int slot) {
    if (slot < 0 || slot >= g_visibleCount) {
        return;
    }

    const int index = g_firstVisible + slot;
    if (index < 0 || index >= static_cast<int>(g_entries.size())) {
        return;
    }

    g_selected = index;
    ActivateSelected();
}

void ActivateCardAtPoint(POINT point) {
    for (int slot = 0; slot < g_visibleCount; ++slot) {
        RECT card = CardRect(slot);
        if (PtInRect(&card, point) != FALSE) {
            ActivateVisibleSlot(slot);
            return;
        }
    }
}

void DrawCardNumber(HDC dc, int slot) {
    if (g_numberFont == nullptr || slot < 0 || slot >= kMaxNumberedCards) {
        return;
    }

    wchar_t number[2]{
        static_cast<wchar_t>(L'1' + slot),
        L'\0'};

    RECT number_rect = PreviewBounds(slot);
    HGDIOBJ previous_font = SelectObject(dc, g_numberFont);
    const COLORREF previous_color = SetTextColor(dc, RGB(128, 128, 128));

    DrawTextW(
        dc,
        number,
        -1,
        &number_rect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetTextColor(dc, previous_color);
    if (previous_font != nullptr) {
        SelectObject(dc, previous_font);
    }
}

void PaintDeck(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);

    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    FrameRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    HGDIOBJ previous_font = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));

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

        DrawCardNumber(dc, slot);
        SetTextColor(dc, RGB(0, 0, 0));

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

void PositionDeck(HWND anchor) {
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
    if (max_visible > kMaxNumberedCards) {
        max_visible = kMaxNumberedCards;
    }

    const int entry_count = static_cast<int>(g_entries.size());
    g_visibleCount = entry_count < max_visible ? entry_count : max_visible;

    const int window_width = (2 * kPadding) +
        (g_visibleCount * kCardWidth) +
        ((g_visibleCount - 1) * kGap);
    const int window_height = (2 * kPadding) + kCardHeight;
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

void SelectPreviousWindow(HWND foreground) {
    g_selected = 0;

    for (size_t i = 0; i < g_entries.size(); ++i) {
        if (g_entries[i].window != foreground) {
            g_selected = static_cast<int>(i);
            return;
        }
    }
}

LRESULT CALLBACK DeckWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT:
        PaintDeck(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN:
        if (wparam >= L'1' && wparam <= L'9') {
            ActivateVisibleSlot(static_cast<int>(wparam - L'1'));
            return 0;
        }

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
            Deck::DismissForPassThrough();
            return 0;
        default:
            break;
        }
        break;
    case WM_LBUTTONDOWN: {
        const POINTS mouse = MAKEPOINTS(lparam);
        ActivateCardAtPoint(POINT{mouse.x, mouse.y});
        return 0;
    }
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE && !g_hiding && IsWindowVisible(window) != FALSE) {
            Deck::Hide();
        }
        return 0;
    case WM_CLOSE:
        Deck::DismissForPassThrough();
        return 0;
    case WM_DESTROY:
        ClearEntries();
        g_returnWindow = nullptr;
        return 0;
    default:
        break;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace

namespace Deck {

bool Initialize(HINSTANCE instance) {
    g_instance = instance;

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DeckWindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    window_class.lpszClassName = kDeckClassName;

    if (RegisterClassW(&window_class) == 0) {
        g_instance = nullptr;
        return false;
    }

    g_window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kDeckClassName,
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
        UnregisterClassW(kDeckClassName, instance);
        g_instance = nullptr;
        return false;
    }

    g_numberFont = CreateFontW(
        -96,
        0,
        0,
        0,
        FW_BOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"");

    if (g_numberFont == nullptr) {
        HWND window = g_window;
        g_window = nullptr;
        DestroyWindow(window);
        UnregisterClassW(kDeckClassName, instance);
        g_instance = nullptr;
        return false;
    }

    g_foregroundEventHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_FOREGROUND,
        nullptr,
        ForegroundEventProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    if (g_foregroundEventHook == nullptr) {
        DeleteObject(g_numberFont);
        g_numberFont = nullptr;
        HWND window = g_window;
        g_window = nullptr;
        DestroyWindow(window);
        UnregisterClassW(kDeckClassName, instance);
        g_instance = nullptr;
        return false;
    }

    g_recentWindows.clear();
    EnumWindows(SeedRecentWindow, 0);
    RememberWindow(GetForegroundWindow());

    return true;
}

void Show() {
    if (g_window == nullptr || IsWindowVisible(g_window) != FALSE) {
        return;
    }

    HWND foreground = GetForegroundWindow();
    RememberWindow(foreground);
    RefreshEntries();
    if (g_entries.empty()) {
        return;
    }

    g_returnWindow = foreground;
    g_firstVisible = 0;
    SelectPreviousWindow(foreground);

    PositionDeck(foreground);
    EnsureSelectedVisible();
    UpdateWindow(g_window);
    RegisterThumbnails();
    UpdateThumbnailLayout();
    InvalidateRect(g_window, nullptr, FALSE);
    FocusDeck();
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
    g_returnWindow = nullptr;
    g_selected = 0;
    g_firstVisible = 0;
    g_visibleCount = 0;
    g_hiding = false;
}

void DismissForPassThrough() {
    HWND target = g_returnWindow;
    Hide();
    ActivateWindow(target);
}

void Shutdown() {
    if (g_foregroundEventHook != nullptr) {
        UnhookWinEvent(g_foregroundEventHook);
        g_foregroundEventHook = nullptr;
    }

    Hide();

    if (g_numberFont != nullptr) {
        DeleteObject(g_numberFont);
        g_numberFont = nullptr;
    }

    g_recentWindows.clear();

    if (g_window != nullptr) {
        HWND window = g_window;
        g_window = nullptr;
        DestroyWindow(window);
    }

    if (g_instance != nullptr) {
        UnregisterClassW(kDeckClassName, g_instance);
        g_instance = nullptr;
    }
}

} // namespace Deck
