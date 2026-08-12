#include <windows.h>

#include <string>

#include "input_dismiss.h"
#include "trigger.h"
#include "version.h"
#include "window_switcher.h"

namespace {
constexpr wchar_t kClassName[] = L"HagenwareWindow";
constexpr wchar_t kWindowTitle[] = L"Hagenware";
const std::wstring kMessage = std::wstring(L"Hagenware ") + Version::kNumber;
constexpr UINT kTriggerMessage = WM_APP + 1;

void SetTriggerEnabled(bool enabled) {
    Trigger::SetEnabled(enabled);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case kTriggerMessage:
        WindowSwitcher::Show();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        DrawTextW(dc, kMessage.c_str(), -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
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

    if (!WindowSwitcher::Initialize(instance)) {
        DestroyWindow(window);
        return 1;
    }

    if (!Trigger::Start(window, kTriggerMessage)) {
        WindowSwitcher::Shutdown();
        DestroyWindow(window);
        return 1;
    }

    if (!InputDismiss::Start(SetTriggerEnabled)) {
        Trigger::Stop();
        WindowSwitcher::Shutdown();
        DestroyWindow(window);
        return 1;
    }

    ShowWindow(window, show_command);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    InputDismiss::Stop();
    Trigger::Stop();
    WindowSwitcher::Shutdown();

    return static_cast<int>(message.wParam);
}
