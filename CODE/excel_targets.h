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
    bool empty = false;
};

bool Initialize();
std::vector<Workbook> DetectWorkbooks();
std::vector<Table> DetectTables(const std::wstring& workbook_key);
void Shutdown();

} // namespace ExcelTargets
