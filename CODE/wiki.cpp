#include "wiki.h"

#include <shellapi.h>
#include <windows.h>

#include <string>
#include <vector>

#include "version.h"

#pragma comment(lib, "shell32.lib")

namespace {
constexpr DWORD kModulePathCapacity = 32768;
constexpr wchar_t kWikiFileName[] = L"Hagenware Wiki.txt";

std::wstring ExecutableDirectory() {
    wchar_t buffer[kModulePathCapacity]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, kModulePathCapacity);
    if (length == 0 || length >= kModulePathCapacity) {
        return {};
    }

    std::wstring path(buffer, static_cast<size_t>(length));
    const size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        return {};
    }

    path.resize(separator + 1);
    return path;
}

std::wstring BuildWiki() {
    std::wstring wiki = L"HAGENWARE WIKI\r\n";
    wiki += L"Version: ";
    wiki += Version::kNumber;
    wiki += L"\r\n\r\n";

    wiki += L"WHAT IS HAGENWARE?\r\n";
    wiki += L"Hagenware is a small native Windows utility that provides fast keyboard-driven window switching, window placement and screenshots.\r\n\r\n";

    wiki += L"GLOBAL SHORTCUTS\r\n";
    wiki += L"- Tap Shift by itself: open Deck.\r\n";
    wiki += L"- Tap Ctrl by itself: open Grid.\r\n";
    wiki += L"- Print Screen is not a Hagenware shortcut.\r\n\r\n";

    wiki += L"DECK - WINDOW SWITCHER\r\n";
    wiki += L"- Shows recent open windows with live DWM previews.\r\n";
    wiki += L"- Mouse wheel: move through the window list, regardless of where the mouse pointer is while Deck is open.\r\n";
    wiki += L"- Arrow keys: move selection.\r\n";
    wiki += L"- Space: activate the selected window.\r\n";
    wiki += L"- Number keys 1-9: jump to one of the nine most recent windows.\r\n";
    wiki += L"- Mouse click on a card: activate that window.\r\n";
    wiki += L"- Mouse click outside Deck: close Deck and pass the click through.\r\n";
    wiki += L"- Screenshot button: capture the full virtual desktop as if Deck and Hagenware's running indicator were not visible.\r\n\r\n";

    wiki += L"GRID - WINDOW PLACEMENT\r\n";
    wiki += L"Grid acts on the window that was focused immediately before Grid opened.\r\n";
    wiki += L"Use the numpad or click a cell:\r\n";
    wiki += L"  7 = top-left quarter\r\n";
    wiki += L"  8 = top half\r\n";
    wiki += L"  9 = top-right quarter\r\n";
    wiki += L"  4 = left half\r\n";
    wiki += L"  5 = maximize / restore\r\n";
    wiki += L"  6 = right half\r\n";
    wiki += L"  1 = bottom-left quarter\r\n";
    wiki += L"  2 = bottom half\r\n";
    wiki += L"  3 = bottom-right quarter\r\n";
    wiki += L"- Screenshot button: capture only the window that was focused before Grid opened, with Grid and Hagenware's running indicator hidden from the saved image.\r\n\r\n";

    wiki += L"SCREENSHOTS\r\n";
    wiki += L"- Screenshots are saved as timestamped BMP files next to Hagenware.exe.\r\n";
    wiki += L"- Existing screenshot files are never overwritten.\r\n\r\n";

    wiki += L"RUNNING INDICATOR\r\n";
    wiki += L"- A thin red line at the bottom-center of the primary monitor work area shows that Hagenware is running.\r\n";
    wiki += L"- The indicator is passive, topmost, non-activating and click-through.\r\n\r\n";

    wiki += L"MAIN WINDOW\r\n";
    wiki += L"- F1 while the Hagenware main window is focused: regenerate this wiki next to Hagenware.exe and open it.\r\n\r\n";

    wiki += L"GENERAL\r\n";
    wiki += L"- Hagenware runs as a normal user and does not require elevation.\r\n";
    wiki += L"- No telemetry, update checks or unsolicited network access.\r\n";
    wiki += L"- Starting a newer Hagenware build can retire an older running build once it is idle.\r\n";

    return wiki;
}

bool WriteAll(HANDLE file, const void* data, DWORD size) {
    if (file == INVALID_HANDLE_VALUE || data == nullptr) {
        return false;
    }

    const auto* bytes = static_cast<const BYTE*>(data);
    DWORD written_total = 0;
    while (written_total < size) {
        DWORD written = 0;
        if (WriteFile(
                file,
                bytes + written_total,
                size - written_total,
                &written,
                nullptr) == FALSE ||
            written == 0) {
            return false;
        }
        written_total += written;
    }

    return true;
}

bool WriteWikiFile(const std::wstring& path, const std::wstring& content) {
    const int utf8_size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        content.data(),
        static_cast<int>(content.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (utf8_size <= 0) {
        return false;
    }

    std::vector<char> utf8(static_cast<size_t>(utf8_size));
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            content.data(),
            static_cast<int>(content.size()),
            utf8.data(),
            utf8_size,
            nullptr,
            nullptr) != utf8_size) {
        return false;
    }

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    constexpr BYTE kUtf8Bom[] = {0xEF, 0xBB, 0xBF};
    const bool saved =
        WriteAll(file, kUtf8Bom, static_cast<DWORD>(sizeof(kUtf8Bom))) &&
        WriteAll(file, utf8.data(), static_cast<DWORD>(utf8.size()));

    CloseHandle(file);
    if (!saved) {
        DeleteFileW(path.c_str());
    }

    return saved;
}
} // namespace

namespace Wiki {

bool Open(HWND owner_window) {
    const std::wstring directory = ExecutableDirectory();
    if (directory.empty()) {
        return false;
    }

    const std::wstring path = directory + kWikiFileName;
    if (!WriteWikiFile(path, BuildWiki())) {
        return false;
    }

    const HINSTANCE result = ShellExecuteW(
        owner_window,
        L"open",
        path.c_str(),
        nullptr,
        directory.c_str(),
        SW_SHOWNORMAL);

    return reinterpret_cast<INT_PTR>(result) > 32;
}

} // namespace Wiki
