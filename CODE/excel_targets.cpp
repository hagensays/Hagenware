#include "excel_targets.h"

#include <windows.h>
#include <oleacc.h>
#include <oleauto.h>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAcc.lib")
#pragma comment(lib, "OleAut32.lib")

namespace {
bool g_available = false;
bool g_ownsComInitialization = false;

HRESULT PropertyId(IDispatch* dispatch, const wchar_t* name, DISPID* id) {
    LPOLESTR mutable_name = const_cast<LPOLESTR>(name);
    return dispatch->GetIDsOfNames(IID_NULL, &mutable_name, 1, LOCALE_USER_DEFAULT, id);
}

HRESULT Property(IDispatch* dispatch, const wchar_t* name, VARIANT* result) {
    VariantInit(result);
    DISPID id = DISPID_UNKNOWN;
    HRESULT hr = PropertyId(dispatch, name, &id);
    if (FAILED(hr)) {
        return hr;
    }
    DISPPARAMS params{};
    return dispatch->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET,
        &params, result, nullptr, nullptr);
}

HRESULT Item(IDispatch* dispatch, LONG index, VARIANT* result) {
    VariantInit(result);
    DISPID id = DISPID_UNKNOWN;
    HRESULT hr = PropertyId(dispatch, L"Item", &id);
    if (FAILED(hr)) {
        return hr;
    }
    VARIANTARG arg{};
    VariantInit(&arg);
    arg.vt = VT_I4;
    arg.lVal = index;
    DISPPARAMS params{};
    params.rgvarg = &arg;
    params.cArgs = 1;
    return dispatch->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET,
        &params, result, nullptr, nullptr);
}

IDispatch* ToDispatch(VARIANT* value) {
    IDispatch* result = nullptr;
    if (value->vt == VT_DISPATCH && value->pdispVal != nullptr) {
        value->pdispVal->AddRef();
        result = value->pdispVal;
    } else if (value->vt == VT_UNKNOWN && value->punkVal != nullptr) {
        value->punkVal->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&result));
    }
    return result;
}

IDispatch* ObjectProperty(IDispatch* dispatch, const wchar_t* name) {
    VARIANT value{};
    if (FAILED(Property(dispatch, name, &value))) {
        return nullptr;
    }
    IDispatch* result = ToDispatch(&value);
    VariantClear(&value);
    return result;
}

IDispatch* CollectionItem(IDispatch* collection, LONG index) {
    VARIANT value{};
    if (FAILED(Item(collection, index, &value))) {
        return nullptr;
    }
    IDispatch* result = ToDispatch(&value);
    VariantClear(&value);
    return result;
}

LONG Count(IDispatch* dispatch) {
    VARIANT value{};
    if (FAILED(Property(dispatch, L"Count", &value))) {
        return 0;
    }
    LONG count = value.vt == VT_I4 ? value.lVal : 0;
    VariantClear(&value);
    return count;
}

std::wstring Text(IDispatch* dispatch, const wchar_t* name) {
    VARIANT value{};
    if (FAILED(Property(dispatch, name, &value))) {
        return {};
    }
    std::wstring text;
    if (value.vt == VT_BSTR && value.bstrVal != nullptr) {
        text.assign(value.bstrVal, SysStringLen(value.bstrVal));
    }
    VariantClear(&value);
    return text;
}

bool Same(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

IDispatch* ExcelApplication(HWND window) {
    IDispatch* native = nullptr;
    if (FAILED(AccessibleObjectFromWindow(window, static_cast<DWORD>(OBJID_NATIVEOM),
            IID_IDispatch, reinterpret_cast<void**>(&native)))) {
        return nullptr;
    }
    IDispatch* application = ObjectProperty(native, L"Application");
    native->Release();
    return application;
}

bool IsExcelWindow(HWND window) {
    wchar_t class_name[32]{};
    return GetClassNameW(window, class_name, 32) > 0 && lstrcmpW(class_name, L"XLMAIN") == 0;
}

BOOL CALLBACK CollectCallback(HWND window, LPARAM context) {
    if (!IsExcelWindow(window)) {
        return TRUE;
    }
    auto* entries = reinterpret_cast<std::vector<ExcelTargets::Workbook>*>(context);
    IDispatch* application = ExcelApplication(window);
    if (application == nullptr) {
        return TRUE;
    }
    IDispatch* workbooks = ObjectProperty(application, L"Workbooks");
    if (workbooks != nullptr) {
        const LONG count = Count(workbooks);
        for (LONG index = 1; index <= count; ++index) {
            IDispatch* workbook = CollectionItem(workbooks, index);
            if (workbook == nullptr) {
                continue;
            }
            const std::wstring name = Text(workbook, L"Name");
            const std::wstring full_name = Text(workbook, L"FullName");
            workbook->Release();
            const std::wstring key = full_name.empty() ? name : full_name;
            bool exists = false;
            for (const ExcelTargets::Workbook& entry : *entries) {
                if (Same(entry.key, key)) {
                    exists = true;
                    break;
                }
            }
            if (!key.empty() && !exists) {
                entries->push_back(ExcelTargets::Workbook{name.empty() ? full_name : name, key});
            }
        }
        workbooks->Release();
    }
    application->Release();
    return TRUE;
}

struct FindContext {
    const std::wstring* key = nullptr;
    IDispatch* workbook = nullptr;
};

BOOL CALLBACK FindCallback(HWND window, LPARAM context_value) {
    if (!IsExcelWindow(window)) {
        return TRUE;
    }
    auto* context = reinterpret_cast<FindContext*>(context_value);
    IDispatch* application = ExcelApplication(window);
    if (application == nullptr) {
        return TRUE;
    }
    IDispatch* workbooks = ObjectProperty(application, L"Workbooks");
    if (workbooks != nullptr) {
        const LONG count = Count(workbooks);
        for (LONG index = 1; index <= count; ++index) {
            IDispatch* workbook = CollectionItem(workbooks, index);
            if (workbook == nullptr) {
                continue;
            }
            const std::wstring name = Text(workbook, L"Name");
            const std::wstring full_name = Text(workbook, L"FullName");
            const std::wstring key = full_name.empty() ? name : full_name;
            if (Same(key, *context->key)) {
                context->workbook = workbook;
                break;
            }
            workbook->Release();
        }
        workbooks->Release();
    }
    application->Release();
    return context->workbook == nullptr ? TRUE : FALSE;
}

IDispatch* FindWorkbook(const std::wstring& key) {
    FindContext context{};
    context.key = &key;
    EnumWindows(FindCallback, reinterpret_cast<LPARAM>(&context));
    return context.workbook;
}

bool IsTableEmpty(IDispatch* table) {
    VARIANT value{};
    if (FAILED(Property(table, L"DataBodyRange", &value))) {
        return false;
    }
    const bool empty = value.vt == VT_EMPTY || value.vt == VT_NULL ||
        (value.vt == VT_DISPATCH && value.pdispVal == nullptr);
    VariantClear(&value);
    return empty;
}
} // namespace

namespace ExcelTargets {

bool Initialize() {
    const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(result)) {
        g_available = true;
        g_ownsComInitialization = true;
    } else if (result == RPC_E_CHANGED_MODE) {
        g_available = true;
    }
    return g_available;
}

std::vector<Workbook> DetectWorkbooks() {
    std::vector<Workbook> entries;
    if (g_available) {
        EnumWindows(CollectCallback, reinterpret_cast<LPARAM>(&entries));
    }
    return entries;
}

std::vector<Table> DetectTables(const std::wstring& workbook_key) {
    std::vector<Table> entries;
    if (!g_available || workbook_key.empty()) {
        return entries;
    }
    IDispatch* workbook = FindWorkbook(workbook_key);
    if (workbook == nullptr) {
        return entries;
    }
    IDispatch* worksheets = ObjectProperty(workbook, L"Worksheets");
    if (worksheets != nullptr) {
        const LONG sheet_count = Count(worksheets);
        for (LONG sheet_index = 1; sheet_index <= sheet_count; ++sheet_index) {
            IDispatch* sheet = CollectionItem(worksheets, sheet_index);
            if (sheet == nullptr) {
                continue;
            }
            const std::wstring sheet_name = Text(sheet, L"Name");
            IDispatch* tables = ObjectProperty(sheet, L"ListObjects");
            if (tables != nullptr) {
                const LONG table_count = Count(tables);
                for (LONG table_index = 1; table_index <= table_count; ++table_index) {
                    IDispatch* table = CollectionItem(tables, table_index);
                    if (table == nullptr) {
                        continue;
                    }
                    const std::wstring table_name = Text(table, L"Name");
                    if (!table_name.empty()) {
                        const std::wstring display = sheet_name.empty()
                            ? table_name
                            : sheet_name + L" - " + table_name;
                        entries.push_back(Table{display, IsTableEmpty(table)});
                    }
                    table->Release();
                }
                tables->Release();
            }
            sheet->Release();
        }
        worksheets->Release();
    }
    workbook->Release();
    return entries;
}

void Shutdown() {
    if (g_ownsComInitialization) {
        CoUninitialize();
    }
    g_available = false;
    g_ownsComInitialization = false;
}

} // namespace ExcelTargets
