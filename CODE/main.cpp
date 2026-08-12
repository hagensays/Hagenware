#include <windows.h>

namespace {
constexpr wchar_t kClassName[] = L"HagenwareWindow";
constexpr wchar_t kWindowTitle[] = L"Hagenware";
constexpr wchar_t kMessage[] = L"Hagenware v0.1.3";
constexpr wchar_t kSuccessClassName[] = L"HagenwareSuccessWindow";
constexpr wchar_t kSuccessMessage[] = L"success";
constexpr UINT kShiftTapMessage = WM_APP + 1;
constexpr int kSuccessWidth = 377;
constexpr int kSuccessHeight = 233;

HWND g_mainWindow = nullptr;
HWND g_successWindow = nullptr;
HHOOK g_keyboardHook = nullptr;
bool g_shiftCandidate = false;
DWORD g_shiftScanCode = 0;

bool IsShiftKey(DWORD virtual_key) {
    return virtual_key == VK_SHIFT || virtual_key == VK_LSHIFT || virtual_key == VK_RSHIFT;
}

void CancelShiftCandidate() {
    g_shiftCandidate = false;
    g_shiftScanCode = 0;
}

void ShowSuccessWindow(HINSTANCE instance) {
    if (g_successWindow != nullptr) {
        DestroyWindow(g_successWindow);
    }

    HWND foreground = GetForegroundWindow();
    HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);

    int x = (GetSystemMetrics(SM_CXSCREEN) - kSuccessWidth) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - kSuccessHeight) / 2;

    if (GetMonitorInfoW(monitor, &monitor_info) != FALSE) {
        const int work_width = static_cast<int>(monitor_info.rcWork.right - monitor_info.rcWork.left);
        const int work_height = static_cast<int>(monitor_info.rcWork.bottom - monitor_info.rcWork.top);
        x = monitor_info.rcWork.left + (work_width - kSuccessWidth) / 2;
        y = monitor_info.rcWork.top + (work_height - kSuccessHeight) / 2;
    }

    g_successWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kSuccessClassName,
        L"",
        WS_POPUP,
        x,
        y,
        kSuccessWidth,
        kSuccessHeight,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (g_successWindow != nullptr) {
        SetWindowPos(
            g_successWindow,
            HWND_TOPMOST,
            x,
            y,
            kSuccessWidth,
            kSuccessHeight,
            SWP_SHOWWINDOW | SWP_NOACTIVATE);
    }
}

LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
        const bool key_down = wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;
        const bool key_up = wparam == WM_KEYUP || wparam == WM_SYSKEYUP;

        if (key_down) {
            if (IsShiftKey(key->vkCode)) {
                if (!g_shiftCandidate) {
                    g_shiftCandidate = true;
                    g_shiftScanCode = key->scanCode;
                } else if (key->scanCode != g_shiftScanCode) {
                    CancelShiftCandidate();
                }
            } else {
                CancelShiftCandidate();
            }
        } else if (key_up) {
            if (g_shiftCandidate && IsShiftKey(key->vkCode) && key->scanCode == g_shiftScanCode) {
                CancelShiftCandidate();
                if (g_mainWindow != nullptr) {
                    PostMessageW(g_mainWindow, kShiftTapMessage, 0, 0);
                }
            } else if (g_shiftCandidate) {
                CancelShiftCandidate();
            }
        }
    }

    return CallNextHookEx(g_keyboardHook, code, wparam, lparam);
}

LRESULT CALLBACK SuccessWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);

        FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
        FrameRect(dc, &client, GetSysColorBrush(COLOR_WINDOWFRAME));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));

        HGDIOBJ previous_font = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(dc, kSuccessMessage, -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (previous_font != nullptr) {
            SelectObject(dc, previous_font);
        }

        EndPaint(window, &paint);
        return 0;
    }
    case WM_LBUTTONUP:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (window == g_successWindow) {
            g_successWindow = nullptr;
        }
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case kShiftTapMessage:
        ShowSuccessWindow(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)));
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        DrawTextW(dc, kMessage, -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        if (g_successWindow != nullptr) {
            DestroyWindow(g_successWindow);
        }
        g_mainWindow = nullptr;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kClassName;

    if (RegisterClassW(&window_class) == 0) {
        return 1;
    }

    WNDCLASSW success_class{};
    success_class.lpfnWndProc = SuccessWindowProc;
    success_class.hInstance = instance;
    success_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    success_class.lpszClassName = kSuccessClassName;

    if (RegisterClassW(&success_class) == 0) {
        return 1;
    }

    HWND window = CreateWindowExW(
        0,
        kClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        720,
        480,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (window == nullptr) {
        return 1;
    }

    g_mainWindow = window;
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, instance, 0);
    if (g_keyboardHook == nullptr) {
        DestroyWindow(window);
        return 1;
    }

    ShowWindow(window, show_command);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    UnhookWindowsHookEx(g_keyboardHook);
    g_keyboardHook = nullptr;

    return static_cast<int>(message.wParam);
}
