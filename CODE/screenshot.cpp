#include "screenshot.h"

#include <windows.h>

#include <cwchar>
#include <string>

namespace {
constexpr wchar_t kIndicatorClassName[] = L"HagenwareScreenshotIndicator";
constexpr wchar_t kDeckClassName[] = L"HagenwareDeck";
constexpr UINT kCaptureMessage = WM_APP + 1;
constexpr int kIndicatorWidth = 10;
constexpr int kIndicatorHeight = 9;
constexpr DWORD kModulePathCapacity = 32768;

HINSTANCE g_instance = nullptr;
HWND g_indicatorWindow = nullptr;
HWINEVENTHOOK g_deckEventHook = nullptr;

bool IsDeckWindow(HWND window) {
    if (window == nullptr) {
        return false;
    }

    wchar_t class_name[64]{};
    const int capacity = static_cast<int>(sizeof(class_name) / sizeof(class_name[0]));
    return GetClassNameW(window, class_name, capacity) > 0 &&
        lstrcmpW(class_name, kDeckClassName) == 0;
}

HWND FindDeckWindow() {
    return FindWindowW(kDeckClassName, nullptr);
}

void PositionIndicator(HWND deck_window) {
    if (g_indicatorWindow == nullptr || deck_window == nullptr) {
        return;
    }

    RECT deck_rect{};
    if (GetWindowRect(deck_window, &deck_rect) == FALSE) {
        return;
    }

    const int x = static_cast<int>(deck_rect.right) - kIndicatorWidth - 2;
    const int y = static_cast<int>(deck_rect.top) + 2;

    SetWindowPos(
        g_indicatorWindow,
        HWND_TOPMOST,
        x,
        y,
        kIndicatorWidth,
        kIndicatorHeight,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void HideIndicator() {
    if (g_indicatorWindow != nullptr && IsWindowVisible(g_indicatorWindow) != FALSE) {
        ShowWindow(g_indicatorWindow, SW_HIDE);
    }
}

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

std::wstring ScreenshotStem() {
    SYSTEMTIME time{};
    GetLocalTime(&time);

    wchar_t buffer[64]{};
    const int written = swprintf_s(
        buffer,
        L"Screenshot_%04u%02u%02u_%02u%02u%02u_%03u",
        static_cast<unsigned int>(time.wYear),
        static_cast<unsigned int>(time.wMonth),
        static_cast<unsigned int>(time.wDay),
        static_cast<unsigned int>(time.wHour),
        static_cast<unsigned int>(time.wMinute),
        static_cast<unsigned int>(time.wSecond),
        static_cast<unsigned int>(time.wMilliseconds));

    if (written <= 0) {
        return {};
    }

    return std::wstring(buffer, static_cast<size_t>(written));
}

HANDLE CreateUniqueScreenshotFile(std::wstring* output_path) {
    if (output_path == nullptr) {
        return INVALID_HANDLE_VALUE;
    }

    const std::wstring directory = ExecutableDirectory();
    const std::wstring stem = ScreenshotStem();
    if (directory.empty() || stem.empty()) {
        return INVALID_HANDLE_VALUE;
    }

    for (unsigned int attempt = 0; attempt < 1000; ++attempt) {
        std::wstring candidate = directory + stem;
        if (attempt > 0) {
            candidate += L"_" + std::to_wstring(attempt + 1);
        }
        candidate += L".bmp";

        HANDLE file = CreateFileW(
            candidate.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (file != INVALID_HANDLE_VALUE) {
            *output_path = candidate;
            return file;
        }

        const DWORD error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
            break;
        }
    }

    return INVALID_HANDLE_VALUE;
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

bool CaptureVirtualDesktop() {
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0) {
        return false;
    }

    const unsigned long long image_size_64 =
        static_cast<unsigned long long>(static_cast<unsigned int>(width)) *
        static_cast<unsigned long long>(static_cast<unsigned int>(height)) *
        4ULL;
    const unsigned long long header_size =
        static_cast<unsigned long long>(sizeof(BITMAPFILEHEADER)) +
        static_cast<unsigned long long>(sizeof(BITMAPINFOHEADER));

    if (image_size_64 > static_cast<unsigned long long>(MAXDWORD) - header_size) {
        return false;
    }

    const DWORD image_size = static_cast<DWORD>(image_size_64);

    HDC screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        return false;
    }

    HDC memory_dc = CreateCompatibleDC(screen_dc);
    if (memory_dc == nullptr) {
        ReleaseDC(nullptr, screen_dc);
        return false;
    }

    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = width;
    bitmap_info.bmiHeader.biHeight = -height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    bitmap_info.bmiHeader.biSizeImage = image_size;

    void* pixel_data = nullptr;
    HBITMAP bitmap = CreateDIBSection(
        screen_dc,
        &bitmap_info,
        DIB_RGB_COLORS,
        &pixel_data,
        nullptr,
        0);

    if (bitmap == nullptr || pixel_data == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        DeleteDC(memory_dc);
        ReleaseDC(nullptr, screen_dc);
        return false;
    }

    HGDIOBJ previous_bitmap = SelectObject(memory_dc, bitmap);
    if (previous_bitmap == nullptr || previous_bitmap == HGDI_ERROR) {
        DeleteObject(bitmap);
        DeleteDC(memory_dc);
        ReleaseDC(nullptr, screen_dc);
        return false;
    }

    const BOOL copied = BitBlt(
        memory_dc,
        0,
        0,
        width,
        height,
        screen_dc,
        x,
        y,
        SRCCOPY | CAPTUREBLT);

    SelectObject(memory_dc, previous_bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);

    if (copied == FALSE) {
        DeleteObject(bitmap);
        return false;
    }

    std::wstring screenshot_path;
    HANDLE file = CreateUniqueScreenshotFile(&screenshot_path);
    if (file == INVALID_HANDLE_VALUE) {
        DeleteObject(bitmap);
        return false;
    }

    BITMAPFILEHEADER file_header{};
    file_header.bfType = 0x4D42;
    file_header.bfOffBits = static_cast<DWORD>(header_size);
    file_header.bfSize = file_header.bfOffBits + image_size;

    const bool saved =
        WriteAll(file, &file_header, static_cast<DWORD>(sizeof(file_header))) &&
        WriteAll(file, &bitmap_info.bmiHeader, static_cast<DWORD>(sizeof(bitmap_info.bmiHeader))) &&
        WriteAll(file, pixel_data, image_size);

    CloseHandle(file);
    DeleteObject(bitmap);

    if (!saved) {
        DeleteFileW(screenshot_path.c_str());
    }

    return saved;
}

void PaintIndicator(HWND window) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);

    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

    HGDIOBJ previous_brush = SelectObject(dc, GetStockObject(WHITE_BRUSH));
    HGDIOBJ previous_pen = SelectObject(dc, GetStockObject(BLACK_PEN));

    Rectangle(dc, 1, 2, 9, 8);
    Rectangle(dc, 3, 1, 7, 3);
    Rectangle(dc, 4, 4, 6, 6);

    if (previous_pen != nullptr) {
        SelectObject(dc, previous_pen);
    }
    if (previous_brush != nullptr) {
        SelectObject(dc, previous_brush);
    }

    EndPaint(window, &paint);
}

LRESULT CALLBACK IndicatorWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT:
        PaintIndicator(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case kCaptureMessage: {
        HWND deck_window = FindDeckWindow();
        if (deck_window != nullptr && IsWindowVisible(deck_window) != FALSE) {
            CaptureVirtualDesktop();
        }
        return 0;
    }
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

void CALLBACK DeckEventProc(
    HWINEVENTHOOK,
    DWORD event,
    HWND window,
    LONG object_id,
    LONG child_id,
    DWORD,
    DWORD) {
    if (object_id != OBJID_WINDOW || child_id != CHILDID_SELF || !IsDeckWindow(window)) {
        return;
    }

    if (event == EVENT_OBJECT_SHOW && IsWindowVisible(window) != FALSE) {
        PositionIndicator(window);
    } else if (event == EVENT_OBJECT_HIDE && IsWindowVisible(window) == FALSE) {
        HideIndicator();
    }
}
} // namespace

namespace Screenshot {

bool Initialize() {
    Shutdown();

    g_instance = GetModuleHandleW(nullptr);
    if (g_instance == nullptr) {
        return false;
    }

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = IndicatorWindowProc;
    window_class.hInstance = g_instance;
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    window_class.lpszClassName = kIndicatorClassName;

    if (RegisterClassW(&window_class) == 0) {
        g_instance = nullptr;
        return false;
    }

    g_indicatorWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        kIndicatorClassName,
        L"",
        WS_POPUP,
        0,
        0,
        kIndicatorWidth,
        kIndicatorHeight,
        nullptr,
        nullptr,
        g_instance,
        nullptr);

    if (g_indicatorWindow == nullptr) {
        UnregisterClassW(kIndicatorClassName, g_instance);
        g_instance = nullptr;
        return false;
    }

    g_deckEventHook = SetWinEventHook(
        EVENT_OBJECT_SHOW,
        EVENT_OBJECT_HIDE,
        nullptr,
        DeckEventProc,
        GetCurrentProcessId(),
        0,
        WINEVENT_OUTOFCONTEXT);

    if (g_deckEventHook == nullptr) {
        DestroyWindow(g_indicatorWindow);
        g_indicatorWindow = nullptr;
        UnregisterClassW(kIndicatorClassName, g_instance);
        g_instance = nullptr;
        return false;
    }

    return true;
}

void RequestCapture() {
    if (g_indicatorWindow != nullptr) {
        PostMessageW(g_indicatorWindow, kCaptureMessage, 0, 0);
    }
}

void Shutdown() {
    if (g_deckEventHook != nullptr) {
        UnhookWinEvent(g_deckEventHook);
        g_deckEventHook = nullptr;
    }

    if (g_indicatorWindow != nullptr) {
        DestroyWindow(g_indicatorWindow);
        g_indicatorWindow = nullptr;
    }

    if (g_instance != nullptr) {
        UnregisterClassW(kIndicatorClassName, g_instance);
        g_instance = nullptr;
    }
}

} // namespace Screenshot
