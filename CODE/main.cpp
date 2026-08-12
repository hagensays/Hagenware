#include <windows.h>

#include <string>

#include "deck.h"
#include "grid.h"
#include "input_dismiss.h"
#include "trigger.h"
#include "version.h"

namespace {
constexpr wchar_t kClassName[] = L"HagenwareWindow";
constexpr wchar_t kWindowTitle[] = L"Hagenware";
const std::wstring kMessage = std::wstring(L"Hagenware ") + Version::kNumber;
constexpr UINT kShiftTriggerMessage = WM_APP + 1;
constexpr UINT kControlTriggerMessage = WM_APP + 2;

void SuppressTriggerModifier(DWORD virtual_key) {
    Trigger::SuppressModifierUntilRelease(virtual_key);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case kShiftTriggerMessage:
        Deck::Show();
        return 0;
    case kControlTriggerMessage:
        Grid::Show();
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

    if (!Deck::Initialize(instance)) {
        DestroyWindow(window);
        return 1;
    }

    if (!Grid::Initialize(instance)) {
        Deck::Shutdown();
        DestroyWindow(window);
        return 1;
    }

    if (!Trigger::Start(window, kShiftTriggerMessage, kControlTriggerMessage)) {
        Grid::Shutdown();
        Deck::Shutdown();
        DestroyWindow(window);
        return 1;
    }

    if (!InputDismiss::Start(SuppressTriggerModifier)) {
        Trigger::Stop();
        Grid::Shutdown();
        Deck::Shutdown();
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
    Grid::Shutdown();
    Trigger::Stop();
    Deck::Shutdown();

    return static_cast<int>(message.wParam);
}
