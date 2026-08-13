#include "scanner_result_controls.h"

#include <strsafe.h>

#include <string>

#include "scanner_layout.h"
#include "scanner_path_controls.h"

namespace {
constexpr wchar_t kScanResultsFileName[] = L"Hagenware.ScanResults.json";
constexpr int kScanButtonWidth = 90;
constexpr int kIndicatorWidth = 72;
constexpr int kProgressHeight = 18;
constexpr int kRawExtensionWidth = 80;
constexpr int kRawExportWidth = 120;
constexpr int kPresetButtonWidth = 150;
constexpr int kPresetTextGap = 12;
constexpr int kRawRefreshButtonId = 2001;
constexpr int kRawNameEditId = 2002;
constexpr int kRawExtensionComboId = 2003;

HINSTANCE g_instance = nullptr;
HWND g_parent = nullptr;
HFONT g_font = nullptr;
HWND g_rawNameEdit = nullptr;
bool g_scanResultsPresent = false;

HWND CreateButton(const wchar_t* text, DWORD extra_style,
    int x, int y, int width, int id) {
    return CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | extra_style,
        x, y, width, ScannerLayout::kControlHeight, g_parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
}

std::wstring SafeFileNameText(std::wstring value) {
    for (wchar_t& character : value) {
        switch (character) {
        case L'\\': case L'/': case L':': case L'*': case L'?':
        case L'"': case L'<': case L'>': case L'|':
            character = L'-';
            break;
        default:
            break;
        }
    }
    return value;
}

void RefreshRawExportName() {
    if (g_rawNameEdit == nullptr) {
        return;
    }

    SYSTEMTIME local_time{};
    GetLocalTime(&local_time);
    wchar_t timestamp[64]{};
    const HRESULT format_result = StringCchPrintfW(timestamp, ARRAYSIZE(timestamp),
        L"%04u-%02u-%02u %02u-%02u-%02u",
        static_cast<unsigned int>(local_time.wYear),
        static_cast<unsigned int>(local_time.wMonth),
        static_cast<unsigned int>(local_time.wDay),
        static_cast<unsigned int>(local_time.wHour),
        static_cast<unsigned int>(local_time.wMinute),
        static_cast<unsigned int>(local_time.wSecond));
    if (FAILED(format_result)) {
        timestamp[0] = L'\0';
    }

    std::wstring root = SafeFileNameText(ScannerPathControls::CurrentScanRoot());
    while (!root.empty() && (root.back() == L'-' || root.back() == L' ')) {
        root.pop_back();
    }
    if (root.empty()) {
        root = L"selected path";
    }
    const std::wstring name = L"Scan of " + root + L" on " + std::wstring(timestamp);
    SetWindowTextW(g_rawNameEdit, name.c_str());
}

bool ScanResultsExist() {
    wchar_t module_path[32768]{};
    const DWORD capacity = static_cast<DWORD>(ARRAYSIZE(module_path));
    const DWORD copied = GetModuleFileNameW(nullptr, module_path, capacity);
    if (copied == 0 || copied >= capacity) {
        return false;
    }

    std::wstring path(module_path, copied);
    const size_t separator = path.find_last_of(L"\\/");
    if (separator != std::wstring::npos) {
        path.resize(separator + 1);
    } else {
        path.clear();
    }
    path += kScanResultsFileName;

    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void RefreshResultState() {
    const bool present = ScanResultsExist();
    if (present != g_scanResultsPresent) {
        g_scanResultsPresent = present;
        if (g_parent != nullptr) {
            InvalidateRect(g_parent, nullptr, FALSE);
        }
    }
}

RECT ProgressBounds(const RECT& client) {
    const int left = ScannerLayout::kOuterMargin + kScanButtonWidth +
        ScannerLayout::kControlGap;
    const int right = client.right - ScannerLayout::kOuterMargin -
        kIndicatorWidth - ScannerLayout::kControlGap;
    const int top = ScannerLayout::RowTop(1) +
        (ScannerLayout::kControlHeight - kProgressHeight) / 2;
    return RECT{left, top, right, top + kProgressHeight};
}

RECT IndicatorBounds(const RECT& client) {
    const int right = client.right - ScannerLayout::kOuterMargin;
    return RECT{right - kIndicatorWidth, ScannerLayout::RowTop(1),
        right, ScannerLayout::RowTop(1) + ScannerLayout::kControlHeight};
}

void DrawPresetHeader(HDC dc, int row_index, const wchar_t* text, const RECT& client) {
    RECT text_rect{
        ScannerLayout::kOuterMargin + kPresetButtonWidth + kPresetTextGap,
        ScannerLayout::RowTop(row_index),
        client.right - ScannerLayout::kOuterMargin,
        ScannerLayout::RowTop(row_index) + ScannerLayout::kControlHeight};
    DrawTextW(dc, text, -1, &text_rect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}
} // namespace

namespace ScannerResultControls {

bool Initialize(HINSTANCE instance, HWND parent, HFONT font) {
    if (instance == nullptr || parent == nullptr || font == nullptr) {
        return false;
    }
    g_instance = instance;
    g_parent = parent;
    g_font = font;

    HWND scan_button = CreateButton(L"Scan", WS_DISABLED,
        ScannerLayout::kOuterMargin, ScannerLayout::RowTop(1),
        kScanButtonWidth, 2100);

    const int raw_name_left = ScannerLayout::kOuterMargin +
        ScannerLayout::kRefreshSize + ScannerLayout::kControlGap;
    const int raw_extension_left = ScannerLayout::kWindowWidth -
        ScannerLayout::kOuterMargin - kRawExportWidth - ScannerLayout::kControlGap -
        kRawExtensionWidth;
    const int raw_export_left = ScannerLayout::kWindowWidth -
        ScannerLayout::kOuterMargin - kRawExportWidth;
    const int raw_name_width = raw_extension_left - ScannerLayout::kControlGap -
        raw_name_left;

    HWND raw_refresh = CreateButton(L"\x21BB", 0,
        ScannerLayout::kOuterMargin, ScannerLayout::RowTop(2),
        ScannerLayout::kRefreshSize, kRawRefreshButtonId);
    g_rawNameEdit = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        raw_name_left, ScannerLayout::RowTop(2), raw_name_width,
        ScannerLayout::kControlHeight, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRawNameEditId)), instance, nullptr);
    HWND extension_combo = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        raw_extension_left, ScannerLayout::RowTop(2), kRawExtensionWidth, 180,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRawExtensionComboId)),
        instance, nullptr);
    HWND raw_export = CreateButton(L"Raw Export", WS_DISABLED,
        raw_export_left, ScannerLayout::RowTop(2), kRawExportWidth, 2101);

    HWND preset_files = CreateButton(L"Preset All Files", WS_DISABLED,
        ScannerLayout::kOuterMargin, ScannerLayout::RowTop(3),
        kPresetButtonWidth, 2102);
    HWND preset_folders = CreateButton(L"Preset All Folders", WS_DISABLED,
        ScannerLayout::kOuterMargin, ScannerLayout::RowTop(4),
        kPresetButtonWidth, 2103);
    HWND preset_evaluation = CreateButton(L"Preset Auswertung", WS_DISABLED,
        ScannerLayout::kOuterMargin, ScannerLayout::RowTop(5),
        kPresetButtonWidth, 2104);

    HWND controls[] = {scan_button, raw_refresh, g_rawNameEdit, extension_combo,
        raw_export, preset_files, preset_folders, preset_evaluation};
    for (HWND control : controls) {
        if (control == nullptr) {
            return false;
        }
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }

    SendMessageW(extension_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"txt"));
    SendMessageW(extension_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"csv"));
    SendMessageW(extension_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"xlsx"));
    SendMessageW(extension_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"xlsm"));
    SendMessageW(extension_combo, CB_SETCURSEL, 2, 0);

    RefreshRawExportName();
    RefreshResultState();
    return true;
}

bool HandleCommand(int control_id, int notification) {
    if (control_id == kRawRefreshButtonId && notification == BN_CLICKED) {
        RefreshRawExportName();
        return true;
    }
    return false;
}

void Paint(HDC dc, const RECT& client) {
    HGDIOBJ previous_font = SelectObject(dc, g_font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));

    const RECT progress = ProgressBounds(client);
    FrameRect(dc, &progress, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    RECT indicator = IndicatorBounds(client);
    HBRUSH indicator_brush = CreateSolidBrush(
        g_scanResultsPresent ? RGB(0, 160, 70) : RGB(0, 120, 215));
    if (indicator_brush != nullptr) {
        FillRect(dc, &indicator, indicator_brush);
        DeleteObject(indicator_brush);
    }
    FrameRect(dc, &indicator, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextW(dc, L"JSON", -1, &indicator,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(dc, RGB(0, 0, 0));

    DrawPresetHeader(dc, 3, L"Full Path; letzte Änderung", client);
    DrawPresetHeader(dc, 4, L"Full Path; letzte Änderung", client);
    DrawPresetHeader(dc, 5,
        L"Ebene 1; Ebene 2; Ebene 3; Verantwortlicher; Löschung?", client);

    if (previous_font != nullptr) {
        SelectObject(dc, previous_font);
    }
}

void RefreshOnShow() {
    RefreshResultState();
}

void Shutdown() {
    g_instance = nullptr;
    g_parent = nullptr;
    g_font = nullptr;
    g_rawNameEdit = nullptr;
    g_scanResultsPresent = false;
}

} // namespace ScannerResultControls
