#pragma once

#include <string>
#include <vector>

namespace ExcelTargets {

struct Workbook {
    std::wstring display;
    std::wstring key;
};

struct Table {
    std::wstring display;
    std::wstring sheet_name;
    std::wstring table_name;
    bool empty = false;
};

bool Initialize();
std::vector<Workbook> DetectWorkbooks();
std::vector<Table> DetectTables(const std::wstring& workbook_key);
bool WritePlaceholderA1(
    const std::wstring& workbook_key,
    const Table* table,
    bool allow_overwrite,
    const wchar_t* text);
void Shutdown();

} // namespace ExcelTargets
