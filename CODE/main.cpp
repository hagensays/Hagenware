#include <windows.h>

#include <string>

#include "deck.h"
#include "grid.h"
#include "input_dismiss.h"
#include "instance_handoff.h"
#include "lifecycle.h"
#include "screenshot.h"
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
    const UINT query_version_message = InstanceHandoff::QueryVersionMessage();
    if (query_version_message != 0 && message == query_version_message) {
        return static_cast<LRESULT>(Version::kPacked);
    }

    const UINT retire_message = InstanceHandoff::RetireWhenIdleMessage();
    if (retire_message != 0 && message == retire_message) {
        Lifecycle::RequestRetire();
        return 1;
    }

    switch (message) {
    case kShiftTriggerMessage:
        if (!Lifecycle::IsRetiring()) {
            Deck::Show();
        }
        return 0;
    case kControlTriggerMessage:
        if (!Lifecycle::IsRetiring()) {
            Grid::Show();
        }
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
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

void DestroyHostWindow(HWND window, HINSTANCE instance) {
    if (window != nullptr && IsWindow(window) != FALSE) {
        DestroyWindow(window);
    }
    UnregisterClassW(kClassName, instance);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    if (!InstanceHandoff::BeginStartup()) {
        return 1;
    }

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kClassName;

    if (RegisterClassW(&window_class) == 0) {
        InstanceHandoff::EndStartup();
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
        UnregisterClassW(kClassName, instance);
        InstanceHandoff::EndStartup();
        return 1;
    }

    Lifecycle::Initialize(window);

    const InstanceHandoff::StartupResult handoff =
        InstanceHandoff::ResolvePreviousInstances(window, Version::kPacked);
    if (handoff != InstanceHandoff::StartupResult::Continue) {
        Lifecycle::Shutdown();
        DestroyHostWindow(window, instance);
        InstanceHandoff::EndStartup();
        return handoff == InstanceHandoff::StartupResult::ExistingSameOrNewer ? 0 : 1;
    }

    if (!Deck::Initialize(instance)) {
        Lifecycle::Shutdown();
        DestroyHostWindow(window, instance);
        InstanceHandoff::EndStartup();
        return 1;
    }

    if (!Grid::Initialize(instance)) {
        Deck::Shutdown();
        Lifecycle::Shutdown();
        DestroyHostWindow(window, instance);
        InstanceHandoff::EndStartup();
        return 1;
    }

    if (!Screenshot::Initialize()) {
        Grid::Shutdown();
        Deck::Shutdown();
        Lifecycle::Shutdown();
        DestroyHostWindow(window, instance);
        InstanceHandoff::EndStartup();
        return 1;
    }

    if (!Trigger::Start(window, kShiftTriggerMessage, kControlTriggerMessage)) {
        Screenshot::Shutdown();
        Grid::Shutdown();
        Deck::Shutdown();
        Lifecycle::Shutdown();
        DestroyHostWindow(window, instance);
        InstanceHandoff::EndStartup();
        return 1;
    }

    if (!InputDismiss::Start(SuppressTriggerModifier)) {
        Trigger::Stop();
        Screenshot::Shutdown();
        Grid::Shutdown();
        Deck::Shutdown();
        Lifecycle::Shutdown();
        DestroyHostWindow(window, instance);
        InstanceHandoff::EndStartup();
        return 1;
    }

    InstanceHandoff::EndStartup();
    ShowWindow(window, show_command);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    InputDismiss::Stop();
    Trigger::Stop();
    Screenshot::Shutdown();
    Grid::Shutdown();
    Deck::Shutdown();
    Lifecycle::Shutdown();
    UnregisterClassW(kClassName, instance);

    return static_cast<int>(message.wParam);
}
