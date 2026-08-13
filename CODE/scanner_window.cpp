#include "scanner_window.h"

#include <string>
#include <vector>

#include "excel_targets.h"
#include "lifecycle.h"

namespace {
constexpr wchar_t kClassName[] = L"HagenwareScannerWindow";
constexpr int kWindowWidth = 640;
constexpr int kWindowHeight = 420;
constexpr int kOuterMargin = 24;
constexpr int kTitleTop = 16;
constexpr int kTitleHeight = 28;
constexpr int kRowOneTop = 56;
constexpr int kRowTwoTop = 96;
constexpr int kRowThreeTop = 136;
constexpr int kControlHeight = 28;
constexpr int kRefreshSize = 28;
constexpr int kDriveWidth = 76;
constexpr int kWorkbookWidth = 180;
constexpr int kTableWidth = 140;
constexpr int kOverwriteWidth = 100;
constexpr int kExportWidth = 112;
constexpr int kScanButtonWidth = 90;
constexpr int kControlGap = 8;
constexpr int kProgressHeight = 18;
constexpr int kDriveRefreshButtonId = 1001;
constexpr int kDriveComboId = 1002;
constexpr int kPathEditId = 1003;
constexpr int kExcelRefreshButtonId = 1004;
constexpr int kWorkbookComboId = 1005;
constexpr int kTableComboId = 1006;
constexpr int kOverwriteCheckId = 1007;
constexpr int kExportButtonId = 1008;
constexpr int kScanButtonId = 1009;

struct NormalizedPath {
    wchar_t drive = L'\0';
    std::wstring relative;
};

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
HWND g_driveRefreshButton = nullptr;
HWND g_driveCombo = nullptr;
HWND g_pathEdit = nullptr;
HWND g_excelRefreshButton = nullptr;
HWND g_workbookCombo = nullptr;
HWND g_tableCombo = nullptr;
HWND g_overwriteCheck = nullptr;
HWND g_exportButton = nullptr;
HWND g_scanButton = nullptr;
HWND g_anchorWindow = nullptr;
HFONT g_titleFont = nullptr;
HFONT g_bodyFont = nullptr;
WNDPROC g_pathEditOriginalProc = nullptr;
std::wstring g_normalizedRelativePath;
wchar_t g_normalizedDrive = L'\0';
std::vector<ExcelTargets::Workbook> g_excelWorkbooks;
std::vector<ExcelTargets::Table> g_excelTables;
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

    if (restore_host &&
        g_anchorWindow != nullptr &&
        IsWindow(g_anchorWindow) != FALSE &&
        IsWindowVisible(g_anchorWindow) != FALSE) {
        SetForegroundWindow(g_anchorWindow);
        SetActiveWindow(g_anchorWindow);
        SetFocus(g_anchorWindow);
    }
}

bool IsAsciiLetter(wchar_t value) {
    return (value >= L'A' && value <= L'Z') ||
        (value >= L'a' && value <= L'z');
}

wchar_t UpperAscii(wchar_t value) {
    if (value >= L'a' && value <= L'z') {
        return static_cast<wchar_t>(value - L'a' + L'A');
    }
    return value;
}

bool SameText(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(
        left.c_str(),
        static_cast<int>(left.size()),
        right.c_str(),
        static_cast<int>(right.size()),
        TRUE) == CSTR_EQUAL;
}

std::wstring ReadControlText(HWND control) {
    if (control == nullptr) {
        return {};
    }

    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return {};
    }

    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(control, text.data(), length + 1);
    if (copied <= 0) {
        return {};
    }

    text.resize(static_cast<size_t>(copied));
    return text;
}

NormalizedPath NormalizePathText(const std::wstring& raw_text) {
    NormalizedPath normalized{};
    size_t start = 0;

    if (raw_text.size() >= 2 && IsAsciiLetter(raw_text[0]) && raw_text[1] == L':') {
        normalized.drive = UpperAscii(raw_text[0]);
        start = 2;
    }

    normalized.relative.reserve(raw_text.size() - start);
    bool previous_separator = false;

    for (size_t index = start; index < raw_text.size(); ++index) {
        const wchar_t value = raw_text[index];
        const bool separator = value == L'\\' || value == L'/';

        if (separator) {
            if (normalized.relative.empty() || previous_separator) {
                previous_separator = true;
                continue;
            }

            normalized.relative.push_back(L'\\');
            previous_separator = true;
            continue;
        }

        normalized.relative.push_back(value);
        previous_separator = false;
    }

    return normalized;
}

wchar_t SelectedDriveLetter() {
    if (g_driveCombo == nullptr) {
        return L'\0';
    }

    const LRESULT selected = SendMessageW(g_driveCombo, CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR) {
        return L'\0';
    }

    wchar_t label[4]{};
    if (SendMessageW(
            g_driveCombo,
            CB_GETLBTEXT,
            static_cast<WPARAM>(selected),
            reinterpret_cast<LPARAM>(label)) == CB_ERR) {
        return L'\0';
    }

    return UpperAscii(label[0]);
}

void RefreshNormalizedPathState() {
    const NormalizedPath normalized = NormalizePathText(ReadControlText(g_pathEdit));
    g_normalizedRelativePath = normalized.relative;
    g_normalizedDrive = normalized.drive != L'\0' ? normalized.drive : SelectedDriveLetter();
}

bool SelectDrive(wchar_t drive) {
    if (g_driveCombo == nullptr || !IsAsciiLetter(drive)) {
        return false;
    }

    wchar_t label[3]{UpperAscii(drive), L':', L'\0'};
    const LRESULT index = SendMessageW(
        g_driveCombo,
        CB_FINDSTRINGEXACT,
        static_cast<WPARAM>(-1),
        reinterpret_cast<LPARAM>(label));
    if (index == CB_ERR) {
        return false;
    }

    SendMessageW(g_driveCombo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
    RefreshNormalizedPathState();
    return true;
}

void RefreshDriveList() {
    if (g_driveCombo == nullptr) {
        return;
    }

    const wchar_t previous_drive = SelectedDriveLetter();
    SendMessageW(g_driveCombo, CB_RESETCONTENT, 0, 0);

    int selected_index = -1;
    const DWORD drives = GetLogicalDrives();
    for (int index = 0; index < 26; ++index) {
        if ((drives & (1u << index)) == 0) {
            continue;
        }

        wchar_t drive_label[3]{
            static_cast<wchar_t>(L'A' + index),
            L':',
            L'\0'};
        const LRESULT added_index = SendMessageW(
            g_driveCombo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(drive_label));
        if (added_index == CB_ERR || added_index == CB_ERRSPACE) {
            continue;
        }

        if (previous_drive != L'\0' && previous_drive == drive_label[0]) {
            selected_index = static_cast<int>(added_index);
        }
    }

    if (selected_index < 0 && SendMessageW(g_driveCombo, CB_GETCOUNT, 0, 0) > 0) {
        selected_index = 0;
    }
    if (selected_index >= 0) {
        SendMessageW(g_driveCombo, CB_SETCURSEL, static_cast<WPARAM>(selected_index), 0);
    }

    RefreshNormalizedPathState();
}

void ApplyNormalizedPath() {
    if (g_pathEdit == nullptr) {
        return;
    }

    const NormalizedPath normalized = NormalizePathText(ReadControlText(g_pathEdit));
    bool drive_selected = true;

    if (normalized.drive != L'\0') {
        RefreshDriveList();
        drive_selected = SelectDrive(normalized.drive);
    }

    std::wstring display_text;
    if (normalized.drive != L'\0' && !drive_selected) {
        display_text.push_back(normalized.drive);
        display_text += L":\\";
        display_text += normalized.relative;
    } else {
        display_text = normalized.relative;
    }

    SetWindowTextW(g_pathEdit, display_text.c_str());
    SendMessageW(
        g_pathEdit,
        EM_SETSEL,
        static_cast<WPARAM>(display_text.size()),
        static_cast<LPARAM>(display_text.size()));
    RefreshNormalizedPathState();
}

std::wstring SelectedWorkbookKey() {
    if (g_workbookCombo == nullptr) {
        return {};
    }

    const LRESULT selected = SendMessageW(g_workbookCombo, CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR) {
        return {};
    }

    const size_t index = static_cast<size_t>(selected);
    if (index >= g_excelWorkbooks.size()) {
        return {};
    }

    return g_excelWorkbooks[index].key;
}

void UpdateExcelControlState() {
    const bool has_workbook =
        g_workbookCombo != nullptr &&
        SendMessageW(g_workbookCombo, CB_GETCURSEL, 0, 0) != CB_ERR;
    const bool has_table =
        g_tableCombo != nullptr &&
        SendMessageW(g_tableCombo, CB_GETCURSEL, 0, 0) != CB_ERR;

    if (g_tableCombo != nullptr) {
        EnableWindow(g_tableCombo, has_workbook ? TRUE : FALSE);
    }
    if (g_overwriteCheck != nullptr) {
        EnableWindow(g_overwriteCheck, has_table ? TRUE : FALSE);
    }

    // Export remains disabled until the Scanner produces results to export.
    if (g_exportButton != nullptr) {
        EnableWindow(g_exportButton, FALSE);
    }
}

void RefreshExcelTables() {
    if (g_tableCombo == nullptr) {
        return;
    }

    SendMessageW(g_tableCombo, CB_RESETCONTENT, 0, 0);
    g_excelTables.clear();

    const std::wstring workbook_key = SelectedWorkbookKey();
    if (!workbook_key.empty()) {
        g_excelTables = ExcelTargets::DetectTables(workbook_key);
        for (const ExcelTargets::Table& entry : g_excelTables) {
            SendMessageW(
                g_tableCombo,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(entry.display.c_str()));
        }

        if (!g_excelTables.empty()) {
            SendMessageW(g_tableCombo, CB_SETCURSEL, 0, 0);
        }
    }

    UpdateExcelControlState();
}

void RefreshExcelWorkbooks() {
    if (g_workbookCombo == nullptr) {
        return;
    }

    const std::wstring previous_key = SelectedWorkbookKey();
    SendMessageW(g_workbookCombo, CB_RESETCONTENT, 0, 0);
    g_excelWorkbooks = ExcelTargets::DetectWorkbooks();

    int selected_index = -1;
    for (size_t index = 0; index < g_excelWorkbooks.size(); ++index) {
        const ExcelTargets::Workbook& entry = g_excelWorkbooks[index];
        SendMessageW(
            g_workbookCombo,
            CB_ADDSTRING,
            0,
            reinterpret_cast<LPARAM>(entry.display.c_str()));
        if (!previous_key.empty() && SameText(previous_key, entry.key)) {
            selected_index = static_cast<int>(index);
        }
    }

    if (selected_index < 0 && !g_excelWorkbooks.empty()) {
        selected_index = 0;
    }
    if (selected_index >= 0) {
        SendMessageW(g_workbookCombo, CB_SETCURSEL, static_cast<WPARAM>(selected_index), 0);
    }

    RefreshExcelTables();
}

RECT ProgressBounds(const RECT& client) {
    const int left = kOuterMargin + kScanButtonWidth + kControlGap;
    const int top = kRowThreeTop + (kControlHeight - kProgressHeight) / 2;
    return RECT{
        left,
        top,
        client.right - kOuterMargin,
        top + kProgressHeight};
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
        kOuterMargin,
        kTitleTop,
        client.right - kOuterMargin,
        kTitleTop + kTitleHeight};
    DrawTextW(
        dc,
        L"Scanner",
        -1,
        &title_rect,
        DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    if (previous_font != nullptr) {
        SelectObject(dc, previous_font);
    }

    const RECT progress = ProgressBounds(client);
    FrameRect(dc, &progress, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    EndPaint(window, &paint);
}

LRESULT CALLBACK PathEditProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_KEYDOWN && wparam == VK_RETURN) {
        ApplyNormalizedPath();
        return 0;
    }
    if (message == WM_CHAR && wparam == VK_RETURN) {
        return 0;
    }

    return CallWindowProcW(g_pathEditOriginalProc, window, message, wparam, lparam);
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
    case WM_COMMAND: {
        const int control_id = LOWORD(wparam);
        const int notification = HIWORD(wparam);

        if (control_id == kDriveRefreshButtonId && notification == BN_CLICKED) {
            RefreshDriveList();
            return 0;
        }
        if (control_id == kExcelRefreshButtonId && notification == BN_CLICKED) {
            RefreshExcelWorkbooks();
            return 0;
        }
        if (control_id == kPathEditId && notification == EN_CHANGE) {
            RefreshNormalizedPathState();
            return 0;
        }
        if (control_id == kDriveComboId && notification == CBN_SELCHANGE) {
            RefreshNormalizedPathState();
            return 0;
        }
        if (control_id == kWorkbookComboId && notification == CBN_SELCHANGE) {
            RefreshExcelTables();
            return 0;
        }
        if ((control_id == kTableComboId && notification == CBN_SELCHANGE) ||
            (control_id == kOverwriteCheckId && notification == BN_CLICKED)) {
            UpdateExcelControlState();
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
        g_driveRefreshButton = nullptr;
        g_driveCombo = nullptr;
        g_pathEdit = nullptr;
        g_excelRefreshButton = nullptr;
        g_workbookCombo = nullptr;
        g_tableCombo = nullptr;
        g_overwriteCheck = nullptr;
        g_exportButton = nullptr;
        g_scanButton = nullptr;
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
    const int x = work_area.left + (width - kWindowWidth) / 2;
    const int y = work_area.top + (height - kWindowHeight) / 2;

    SetWindowPos(
        g_window,
        nullptr,
        x,
        y,
        kWindowWidth,
        kWindowHeight,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

bool CreateControls() {
    const int drive_left = kOuterMargin + kRefreshSize + kControlGap;
    const int path_left = drive_left + kDriveWidth + kControlGap;
    const int path_width = kWindowWidth - path_left - kOuterMargin;

    const int workbook_left = kOuterMargin + kRefreshSize + kControlGap;
    const int table_left = workbook_left + kWorkbookWidth + kControlGap;
    const int overwrite_left = table_left + kTableWidth + kControlGap;
    const int export_left = overwrite_left + kOverwriteWidth + kControlGap;

    g_driveRefreshButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"\x21BB",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kOuterMargin,
        kRowOneTop,
        kRefreshSize,
        kRefreshSize,
        g_window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDriveRefreshButtonId)),
        g_instance,
        nullptr);
    if (g_driveRefreshButton == nullptr) {
        return false;
    }

    g_driveCombo = CreateWindowExW(
        0,
        L"COMBOBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        drive_left,
        kRowOneTop,
        kDriveWidth,
        220,
        g_window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDriveComboId)),
        g_instance,
        nullptr);
    if (g_driveCombo == nullptr) {
        return false;
    }

    g_pathEdit = CreateWindowExW(
        0,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        path_left,
        kRowOneTop,
        path_width,
        kControlHeight,
        g_window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPathEditId)),
        g_instance,
        nullptr);
    if (g_pathEdit == nullptr) {
        return false;
    }

    g_excelRefreshButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"\x21BB",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kOuterMargin,
        kRowTwoTop,
        kRefreshSize,
        kRefreshSize,
        g_window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kExcelRefreshButtonId)),
        g_instance,
        nullptr);
    if (g_excelRefreshButton == nullptr) {
        return false;
    }

    g_workbookCombo = CreateWindowExW(
        0,
        L"COMBOBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        workbook_left,
        kRowTwoTop,
        kWorkbookWidth,
        220,
        g_window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWorkbookComboId)),
        g_instance,
        nullptr);
    if (g_workbookCombo == nullptr) {
        return false;
    }

    g_tableCombo = CreateWindowExW(
        0,
        L"COMBOBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        table_left,
        kRowTwoTop,
        kTableWidth,
        220,
        g_window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTableComboId)),
        g_instance,
        nullptr);
    if (g_tableCombo == nullptr) {
        return false;
    }

    g_overwriteCheck = CreateWindowExW(
        0,
        L"BUTTON",
        L"Overwrite?",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        overwrite_left,
        kRowTwoTop,
        kOverwriteWidth,
        kControlHeight,
        g_window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOverwriteCheckId)),
        g_instance,
        nullptr);
    if (g_overwriteCheck == nullptr) {
        return false;
    }

    g_exportButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"Export",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED | BS_PUSHBUTTON,
        export_left,
        kRowTwoTop,
        kExportWidth,
        kControlHeight,
        g_window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kExportButtonId)),
        g_instance,
        nullptr);
    if (g_exportButton == nullptr) {
        return false;
    }

    g_scanButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"Scan",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED | BS_PUSHBUTTON,
        kOuterMargin,
        kRowThreeTop,
        kScanButtonWidth,
        kControlHeight,
        g_window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kScanButtonId)),
        g_instance,
        nullptr);
    if (g_scanButton == nullptr) {
        return false;
    }

    HWND controls[] = {
        g_driveRefreshButton,
        g_driveCombo,
        g_pathEdit,
        g_excelRefreshButton,
        g_workbookCombo,
        g_tableCombo,
        g_overwriteCheck,
        g_exportButton,
        g_scanButton};
    for (HWND control : controls) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_bodyFont), TRUE);
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous_proc = SetWindowLongPtrW(
        g_pathEdit,
        GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(PathEditProc));
    if (previous_proc == 0 && GetLastError() != ERROR_SUCCESS) {
        return false;
    }
    g_pathEditOriginalProc = reinterpret_cast<WNDPROC>(previous_proc);

    RefreshDriveList();
    RefreshNormalizedPathState();
    UpdateExcelControlState();
    return true;
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
    ExcelTargets::Initialize();

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    window_class.lpszClassName = kClassName;

    if (RegisterClassW(&window_class) == 0) {
        ExcelTargets::Shutdown();
        g_instance = nullptr;
        return false;
    }

    g_bodyFont = CreateFontW(
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
    if (g_bodyFont == nullptr) {
        UnregisterClassW(kClassName, g_instance);
        ExcelTargets::Shutdown();
        g_instance = nullptr;
        return false;
    }

    g_titleFont = CreateFontW(
        -20,
        0,
        0,
        0,
        FW_SEMIBOLD,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
    if (g_titleFont == nullptr) {
        DeleteObject(g_bodyFont);
        g_bodyFont = nullptr;
        UnregisterClassW(kClassName, g_instance);
        ExcelTargets::Shutdown();
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
        instance,
        nullptr);

    if (g_window == nullptr) {
        DeleteObject(g_titleFont);
        DeleteObject(g_bodyFont);
        g_titleFont = nullptr;
        g_bodyFont = nullptr;
        UnregisterClassW(kClassName, g_instance);
        ExcelTargets::Shutdown();
        g_instance = nullptr;
        return false;
    }

    if (!CreateControls()) {
        HWND window = g_window;
        DestroyWindow(window);
        DeleteObject(g_titleFont);
        DeleteObject(g_bodyFont);
        g_titleFont = nullptr;
        g_bodyFont = nullptr;
        g_pathEditOriginalProc = nullptr;
        UnregisterClassW(kClassName, g_instance);
        ExcelTargets::Shutdown();
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

    RefreshDriveList();
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

    ExcelTargets::Shutdown();

    g_driveRefreshButton = nullptr;
    g_driveCombo = nullptr;
    g_pathEdit = nullptr;
    g_excelRefreshButton = nullptr;
    g_workbookCombo = nullptr;
    g_tableCombo = nullptr;
    g_overwriteCheck = nullptr;
    g_exportButton = nullptr;
    g_scanButton = nullptr;
    g_anchorWindow = nullptr;
    g_pathEditOriginalProc = nullptr;
    g_normalizedRelativePath.clear();
    g_normalizedDrive = L'\0';
    g_excelWorkbooks.clear();
    g_excelTables.clear();
    g_positionInitialized = false;
}

} // namespace ScannerWindow
