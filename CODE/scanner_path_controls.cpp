#include "scanner_path_controls.h"

#include "scanner_layout.h"

namespace {
constexpr int kDriveWidth = 76;
constexpr int kDriveRefreshButtonId = 1001;
constexpr int kDriveComboId = 1002;
constexpr int kPathEditId = 1003;

HWND g_driveCombo = nullptr;
HWND g_pathEdit = nullptr;
WNDPROC g_pathEditOriginalProc = nullptr;
std::wstring g_normalizedRelativePath;
wchar_t g_normalizedDrive = L'\0';

struct NormalizedPath {
    wchar_t drive = L'\0';
    std::wstring relative;
};

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
    if (SendMessageW(g_driveCombo, CB_GETLBTEXT, static_cast<WPARAM>(selected),
            reinterpret_cast<LPARAM>(label)) == CB_ERR) {
        return L'\0';
    }
    return UpperAscii(label[0]);
}

void RefreshNormalizedState() {
    const NormalizedPath normalized = NormalizePathText(ReadControlText(g_pathEdit));
    g_normalizedRelativePath = normalized.relative;
    g_normalizedDrive = normalized.drive != L'\0'
        ? normalized.drive
        : SelectedDriveLetter();
}

bool SelectDrive(wchar_t drive) {
    if (g_driveCombo == nullptr || !IsAsciiLetter(drive)) {
        return false;
    }
    wchar_t label[3]{UpperAscii(drive), L':', L'\0'};
    const LRESULT index = SendMessageW(g_driveCombo, CB_FINDSTRINGEXACT,
        static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(label));
    if (index == CB_ERR) {
        return false;
    }
    SendMessageW(g_driveCombo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
    RefreshNormalizedState();
    return true;
}

void ApplyNormalizedPath() {
    if (g_pathEdit == nullptr) {
        return;
    }
    const NormalizedPath normalized = NormalizePathText(ReadControlText(g_pathEdit));
    bool drive_selected = true;
    if (normalized.drive != L'\0') {
        ScannerPathControls::RefreshDrives();
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
    SendMessageW(g_pathEdit, EM_SETSEL,
        static_cast<WPARAM>(display_text.size()),
        static_cast<LPARAM>(display_text.size()));
    RefreshNormalizedState();
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
} // namespace

namespace ScannerPathControls {

bool Initialize(HINSTANCE instance, HWND parent, HFONT font) {
    if (instance == nullptr || parent == nullptr || font == nullptr) {
        return false;
    }

    const int drive_left = ScannerLayout::kOuterMargin +
        ScannerLayout::kRefreshSize + ScannerLayout::kControlGap;
    const int path_left = drive_left + kDriveWidth + ScannerLayout::kControlGap;
    const int path_width = ScannerLayout::kWindowWidth -
        ScannerLayout::kOuterMargin - path_left;

    HWND refresh_button = CreateWindowExW(0, L"BUTTON", L"\x21BB",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        ScannerLayout::kOuterMargin, ScannerLayout::RowTop(0),
        ScannerLayout::kRefreshSize, ScannerLayout::kControlHeight,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDriveRefreshButtonId)),
        instance, nullptr);

    g_driveCombo = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        drive_left, ScannerLayout::RowTop(0), kDriveWidth, 220,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDriveComboId)),
        instance, nullptr);

    g_pathEdit = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        path_left, ScannerLayout::RowTop(0), path_width, ScannerLayout::kControlHeight,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPathEditId)),
        instance, nullptr);

    HWND controls[] = {refresh_button, g_driveCombo, g_pathEdit};
    for (HWND control : controls) {
        if (control == nullptr) {
            return false;
        }
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous_proc = SetWindowLongPtrW(g_pathEdit, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(PathEditProc));
    if (previous_proc == 0 && GetLastError() != ERROR_SUCCESS) {
        return false;
    }
    g_pathEditOriginalProc = reinterpret_cast<WNDPROC>(previous_proc);

    RefreshDrives();
    RefreshNormalizedState();
    return true;
}

bool HandleCommand(int control_id, int notification) {
    if (control_id == kDriveRefreshButtonId && notification == BN_CLICKED) {
        RefreshDrives();
        return true;
    }
    if (control_id == kPathEditId && notification == EN_CHANGE) {
        RefreshNormalizedState();
        return true;
    }
    if (control_id == kDriveComboId && notification == CBN_SELCHANGE) {
        RefreshNormalizedState();
        return true;
    }
    return false;
}

void RefreshDrives() {
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
        wchar_t drive_label[3]{static_cast<wchar_t>(L'A' + index), L':', L'\0'};
        const LRESULT added_index = SendMessageW(g_driveCombo, CB_ADDSTRING, 0,
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
        SendMessageW(g_driveCombo, CB_SETCURSEL,
            static_cast<WPARAM>(selected_index), 0);
    }
    RefreshNormalizedState();
}

std::wstring CurrentScanRoot() {
    RefreshNormalizedState();
    std::wstring root;
    if (g_normalizedDrive != L'\0') {
        root.push_back(g_normalizedDrive);
        root += L":\\";
    }
    root += g_normalizedRelativePath;
    if (!root.empty() && root.back() != L'\\') {
        root.push_back(L'\\');
    }
    return root;
}

void Shutdown() {
    g_driveCombo = nullptr;
    g_pathEdit = nullptr;
    g_pathEditOriginalProc = nullptr;
    g_normalizedRelativePath.clear();
    g_normalizedDrive = L'\0';
}

} // namespace ScannerPathControls
