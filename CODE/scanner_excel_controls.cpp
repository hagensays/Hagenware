#include "scanner_excel_controls.h"

#include <string>
#include <vector>

#include "excel_targets.h"
#include "scanner_layout.h"

namespace {
constexpr int kWorkbookWidth = 180;
constexpr int kTableWidth = 140;
constexpr int kOverwriteWidth = 100;
constexpr int kExportWidth = 112;
constexpr int kRefreshButtonId = 3001;
constexpr int kWorkbookComboId = 3002;
constexpr int kTableComboId = 3003;
constexpr int kOverwriteCheckId = 3004;
constexpr int kExportButtonId = 3005;

HINSTANCE g_instance = nullptr;
HWND g_parent = nullptr;
HWND g_workbookCombo = nullptr;
HWND g_tableCombo = nullptr;
HWND g_overwriteCheck = nullptr;
HWND g_exportButton = nullptr;
std::vector<ExcelTargets::Workbook> g_workbooks;
std::vector<ExcelTargets::Table> g_tables;

bool SameText(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
        right.c_str(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
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
    return index < g_workbooks.size() ? g_workbooks[index].key : std::wstring{};
}

const ExcelTargets::Table* SelectedTable() {
    if (g_tableCombo == nullptr) {
        return nullptr;
    }
    const LRESULT selected = SendMessageW(g_tableCombo, CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR) {
        return nullptr;
    }
    const size_t index = static_cast<size_t>(selected);
    return index < g_tables.size() ? &g_tables[index] : nullptr;
}

void UpdateState() {
    const bool has_workbook = g_workbookCombo != nullptr &&
        SendMessageW(g_workbookCombo, CB_GETCURSEL, 0, 0) != CB_ERR;
    if (g_tableCombo != nullptr) {
        EnableWindow(g_tableCombo, has_workbook ? TRUE : FALSE);
    }
    if (g_overwriteCheck != nullptr) {
        EnableWindow(g_overwriteCheck, has_workbook ? TRUE : FALSE);
    }
    if (g_exportButton != nullptr) {
        EnableWindow(g_exportButton, has_workbook ? TRUE : FALSE);
    }
}

void RefreshTables() {
    if (g_tableCombo == nullptr) {
        return;
    }
    SendMessageW(g_tableCombo, CB_RESETCONTENT, 0, 0);
    g_tables.clear();
    const std::wstring workbook_key = SelectedWorkbookKey();
    if (!workbook_key.empty()) {
        g_tables = ExcelTargets::DetectTables(workbook_key);
        for (const ExcelTargets::Table& table : g_tables) {
            SendMessageW(g_tableCombo, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(table.display.c_str()));
        }
        if (!g_tables.empty()) {
            SendMessageW(g_tableCombo, CB_SETCURSEL, 0, 0);
        }
    }
    UpdateState();
}

void RefreshWorkbooks() {
    if (g_workbookCombo == nullptr) {
        return;
    }
    const std::wstring previous_key = SelectedWorkbookKey();
    SendMessageW(g_workbookCombo, CB_RESETCONTENT, 0, 0);
    g_workbooks = ExcelTargets::DetectWorkbooks();

    int selected_index = -1;
    for (size_t index = 0; index < g_workbooks.size(); ++index) {
        const ExcelTargets::Workbook& workbook = g_workbooks[index];
        SendMessageW(g_workbookCombo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(workbook.display.c_str()));
        if (!previous_key.empty() && SameText(previous_key, workbook.key)) {
            selected_index = static_cast<int>(index);
        }
    }
    if (selected_index < 0 && !g_workbooks.empty()) {
        selected_index = 0;
    }
    if (selected_index >= 0) {
        SendMessageW(g_workbookCombo, CB_SETCURSEL,
            static_cast<WPARAM>(selected_index), 0);
    }
    RefreshTables();
}

void ExportPlaceholder() {
    const std::wstring workbook_key = SelectedWorkbookKey();
    if (workbook_key.empty()) {
        return;
    }
    const bool overwrite = g_overwriteCheck != nullptr &&
        SendMessageW(g_overwriteCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!ExcelTargets::WritePlaceholderA1(workbook_key, SelectedTable(), overwrite,
            L"no scanner results yet")) {
        MessageBeep(MB_ICONWARNING);
    }
}
} // namespace

namespace ScannerExcelControls {

bool Initialize(HINSTANCE instance, HWND parent, HFONT font) {
    if (instance == nullptr || parent == nullptr || font == nullptr) {
        return false;
    }
    g_instance = instance;
    g_parent = parent;
    ExcelTargets::Initialize();

    const int workbook_left = ScannerLayout::kOuterMargin +
        ScannerLayout::kRefreshSize + ScannerLayout::kControlGap;
    const int table_left = workbook_left + kWorkbookWidth + ScannerLayout::kControlGap;
    const int overwrite_left = table_left + kTableWidth + ScannerLayout::kControlGap;
    const int export_left = overwrite_left + kOverwriteWidth + ScannerLayout::kControlGap;

    HWND refresh_button = CreateWindowExW(0, L"BUTTON", L"\x21BB",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        ScannerLayout::kOuterMargin, ScannerLayout::RowTop(6),
        ScannerLayout::kRefreshSize, ScannerLayout::kControlHeight,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRefreshButtonId)),
        instance, nullptr);

    g_workbookCombo = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        workbook_left, ScannerLayout::RowTop(6), kWorkbookWidth, 220,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWorkbookComboId)),
        instance, nullptr);

    g_tableCombo = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        table_left, ScannerLayout::RowTop(6), kTableWidth, 220,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTableComboId)),
        instance, nullptr);

    g_overwriteCheck = CreateWindowExW(0, L"BUTTON", L"Overwrite?",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        overwrite_left, ScannerLayout::RowTop(6), kOverwriteWidth,
        ScannerLayout::kControlHeight, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOverwriteCheckId)),
        instance, nullptr);

    g_exportButton = CreateWindowExW(0, L"BUTTON", L"Export",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED | BS_PUSHBUTTON,
        export_left, ScannerLayout::RowTop(6), kExportWidth,
        ScannerLayout::kControlHeight, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kExportButtonId)),
        instance, nullptr);

    HWND controls[] = {refresh_button, g_workbookCombo, g_tableCombo,
        g_overwriteCheck, g_exportButton};
    for (HWND control : controls) {
        if (control == nullptr) {
            return false;
        }
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }

    UpdateState();
    return true;
}

bool HandleCommand(int control_id, int notification) {
    if (control_id == kRefreshButtonId && notification == BN_CLICKED) {
        RefreshWorkbooks();
        return true;
    }
    if (control_id == kWorkbookComboId && notification == CBN_SELCHANGE) {
        RefreshTables();
        return true;
    }
    if ((control_id == kTableComboId && notification == CBN_SELCHANGE) ||
        (control_id == kOverwriteCheckId && notification == BN_CLICKED)) {
        UpdateState();
        return true;
    }
    if (control_id == kExportButtonId && notification == BN_CLICKED) {
        ExportPlaceholder();
        return true;
    }
    return false;
}

void Shutdown() {
    ExcelTargets::Shutdown();
    g_instance = nullptr;
    g_parent = nullptr;
    g_workbookCombo = nullptr;
    g_tableCombo = nullptr;
    g_overwriteCheck = nullptr;
    g_exportButton = nullptr;
    g_workbooks.clear();
    g_tables.clear();
}

} // namespace ScannerExcelControls
