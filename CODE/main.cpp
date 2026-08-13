#include <windows.h>

#include <string>

#include "deck.h"
#include "grid.h"
#include "input_dismiss.h"
#include "instance_handoff.h"
#include "lifecycle.h"
#include "screenshot.h"
#include "status_indicator.h"
#include "trigger.h"
#include "version.h"
#include "wiki.h"

namespace {
constexpr wchar_t kClassName[] = L"HagenwareWindow";
constexpr wchar_t kWindowTitle[] = L"Hagenware";
const std::wstring kMessage = std::wstring(L"Hagenware ") + Version::kNumber;
constexpr UINT kShiftTriggerMessage = WM_APP + 1;
constexpr UINT kControlTriggerMessage = WM_APP + 2;

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
        if (!Trigger::AcceptPostedTrigger(message, wparam)) {
            return 0;
        }
        if (!Lifecycle::IsRetiring()) {
            if (Grid::IsVisible()) {
                Grid::DismissForPassThrough();
            }
            Deck::Show();
        }
        return 0;
    case kControlTriggerMessage:
        if (!Trigger::AcceptPostedTrigger(message, wparam)) {
            return 0;
        }
        if (!Lifecycle::IsRetiring()) {
            if (Deck::IsVisible()) {
                Deck::DismissForPassThrough();
            }
            Grid::Show();
        }
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_F1 && GetForegroundWindow() == window) {
            if (!Wiki::Open(window)) {
                MessageBoxW(
                    window,
                    L"Hagenware Wiki.txt could not be created or opened next to Hagenware.exe.",
                    L"Hagenware Wiki",
                    MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        break;
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

    return DefWindowProcW(window, message, wparam, lparam);
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

    if (!InputDismiss::Start()) {
        Screenshot::Shutdown();
        Grid::Shutdown();
        Deck::Shutdown();
        Lifecycle::Shutdown();
        DestroyHostWindow(window, instance);
        InstanceHandoff::EndStartup();
        return 1;
    }

    if (!Trigger::Start(window, kShiftTriggerMessage, kControlTriggerMessage)) {
        InputDismiss::Stop();
        Screenshot::Shutdown();
        Grid::Shutdown();
        Deck::Shutdown();
        Lifecycle::Shutdown();
        DestroyHostWindow(window, instance);
        InstanceHandoff::EndStartup();
        return 1;
    }

    if (!StatusIndicator::Initialize(instance)) {
        Trigger::Stop();
        InputDismiss::Stop();
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

    int exit_code = 0;
    MSG message{};
    for (;;) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            continue;
        }

        if (result == 0) {
            exit_code = static_cast<int>(message.wParam);
        } else {
            exit_code = 1;
        }
        break;
    }

    StatusIndicator::Shutdown();
    Trigger::Stop();
    InputDismiss::Stop();
    Screenshot::Shutdown();
    Grid::Shutdown();
    Deck::Shutdown();
    Lifecycle::Shutdown();
    UnregisterClassW(kClassName, instance);

    return exit_code;
}
