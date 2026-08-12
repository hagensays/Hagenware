#include "instance_handoff.h"

#include <vector>

namespace {
constexpr wchar_t kStartupMutexName[] = L"Local\\Hagenware.StartupHandoff.v1";
constexpr wchar_t kHostClassName[] = L"HagenwareWindow";
constexpr wchar_t kDeckClassName[] = L"HagenwareDeck";
constexpr wchar_t kGridClassName[] = L"HagenwareGrid";
constexpr wchar_t kLegacyDeckClassName[] = L"HagenwareWindowSwitcher";
constexpr wchar_t kLegacyGridClassName[] = L"HagenwareWindowPlacement";
constexpr wchar_t kQueryVersionMessageName[] = L"Hagenware.QueryVersion.v1";
constexpr wchar_t kRetireMessageName[] = L"Hagenware.RetireWhenIdle.v1";
constexpr UINT kMessageTimeoutMs = 500;

HANDLE g_startupMutex = nullptr;

struct ExistingInstance {
    HWND host = nullptr;
    DWORD processId = 0;
    unsigned int version = 0;
};

struct HostSearchContext {
    HWND currentWindow = nullptr;
    std::vector<ExistingInstance>* instances = nullptr;
};

struct BusySearchContext {
    DWORD processId = 0;
    bool busy = false;
};

bool HasClassName(HWND window, const wchar_t* expected) {
    wchar_t class_name[64]{};
    const int capacity = static_cast<int>(sizeof(class_name) / sizeof(class_name[0]));
    return GetClassNameW(window, class_name, capacity) > 0 && lstrcmpW(class_name, expected) == 0;
}

bool IsKnownSurfaceWindow(HWND window) {
    return HasClassName(window, kDeckClassName) ||
        HasClassName(window, kGridClassName) ||
        HasClassName(window, kLegacyDeckClassName) ||
        HasClassName(window, kLegacyGridClassName);
}

BOOL CALLBACK FindHostWindow(HWND window, LPARAM context_value) {
    auto* context = reinterpret_cast<HostSearchContext*>(context_value);
    if (context == nullptr || context->instances == nullptr || window == context->currentWindow) {
        return TRUE;
    }

    if (!HasClassName(window, kHostClassName)) {
        return TRUE;
    }

    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id == 0 || process_id == GetCurrentProcessId()) {
        return TRUE;
    }

    for (const ExistingInstance& instance : *context->instances) {
        if (instance.processId == process_id) {
            return TRUE;
        }
    }

    context->instances->push_back(ExistingInstance{window, process_id, 0});
    return TRUE;
}

BOOL CALLBACK FindBusySurface(HWND window, LPARAM context_value) {
    auto* context = reinterpret_cast<BusySearchContext*>(context_value);
    if (context == nullptr || IsWindowVisible(window) == FALSE) {
        return TRUE;
    }

    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id != context->processId) {
        return TRUE;
    }

    if (IsKnownSurfaceWindow(window)) {
        context->busy = true;
        return FALSE;
    }

    return TRUE;
}

bool IsLegacyInstanceBusy(DWORD process_id) {
    BusySearchContext context{};
    context.processId = process_id;
    EnumWindows(FindBusySurface, reinterpret_cast<LPARAM>(&context));
    return context.busy;
}

void CALLBACK LegacySurfaceEventProc(
    HWINEVENTHOOK,
    DWORD,
    HWND window,
    LONG object_id,
    LONG child_id,
    DWORD,
    DWORD) {
    if (window == nullptr || object_id != OBJID_WINDOW || child_id != CHILDID_SELF) {
        return;
    }

    if (IsKnownSurfaceWindow(window)) {
        // The callback itself wakes the startup message pump. The caller rechecks
        // current visibility after the queued WinEvent has been dispatched.
    }
}

bool PumpPendingMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
        if (message.message == WM_QUIT) {
            PostQuitMessage(static_cast<int>(message.wParam));
            return false;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return true;
}

bool WaitForProcessExit(HANDLE process) {
    if (process == nullptr) {
        return false;
    }

    for (;;) {
        const DWORD wait_result = MsgWaitForMultipleObjects(
            1,
            &process,
            FALSE,
            INFINITE,
            QS_ALLINPUT);

        if (wait_result == WAIT_OBJECT_0) {
            return true;
        }

        if (wait_result == WAIT_OBJECT_0 + 1) {
            if (!PumpPendingMessages()) {
                return false;
            }
            continue;
        }

        return false;
    }
}

unsigned int QueryRunningVersion(HWND host) {
    const UINT message = InstanceHandoff::QueryVersionMessage();
    if (message == 0 || host == nullptr) {
        return 0;
    }

    DWORD_PTR response = 0;
    const LRESULT sent = SendMessageTimeoutW(
        host,
        message,
        0,
        0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK,
        kMessageTimeoutMs,
        &response);

    if (sent == 0) {
        return 0;
    }

    return static_cast<unsigned int>(response);
}

bool RetireProtocolInstance(const ExistingInstance& instance) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, instance.processId);
    if (process == nullptr) {
        return false;
    }

    const UINT message = InstanceHandoff::RetireWhenIdleMessage();
    DWORD_PTR response = 0;
    const LRESULT sent = message != 0
        ? SendMessageTimeoutW(
            instance.host,
            message,
            0,
            0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK,
            kMessageTimeoutMs,
            &response)
        : 0;

    if (sent == 0 || response == 0) {
        CloseHandle(process);
        return false;
    }

    const bool exited = WaitForProcessExit(process);
    CloseHandle(process);
    return exited;
}

bool RetireLegacyInstance(const ExistingInstance& instance) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, instance.processId);
    if (process == nullptr) {
        return false;
    }

    HWINEVENTHOOK visibility_hook = nullptr;

    if (IsLegacyInstanceBusy(instance.processId)) {
        visibility_hook = SetWinEventHook(
            EVENT_OBJECT_SHOW,
            EVENT_OBJECT_HIDE,
            nullptr,
            LegacySurfaceEventProc,
            instance.processId,
            0,
            WINEVENT_OUTOFCONTEXT);

        if (visibility_hook == nullptr) {
            CloseHandle(process);
            return false;
        }

        while (IsLegacyInstanceBusy(instance.processId)) {
            const DWORD wait_result = MsgWaitForMultipleObjects(
                1,
                &process,
                FALSE,
                INFINITE,
                QS_ALLINPUT);

            if (wait_result == WAIT_OBJECT_0) {
                UnhookWinEvent(visibility_hook);
                CloseHandle(process);
                return true;
            }

            if (wait_result != WAIT_OBJECT_0 + 1 || !PumpPendingMessages()) {
                UnhookWinEvent(visibility_hook);
                CloseHandle(process);
                return false;
            }
        }
    }

    if (visibility_hook != nullptr) {
        UnhookWinEvent(visibility_hook);
    }

    if (IsWindow(instance.host) != FALSE) {
        PostMessageW(instance.host, WM_CLOSE, 0, 0);
    }

    const bool exited = WaitForProcessExit(process);
    CloseHandle(process);
    return exited;
}
} // namespace

namespace InstanceHandoff {

bool BeginStartup() {
    if (g_startupMutex != nullptr) {
        return true;
    }

    g_startupMutex = CreateMutexW(nullptr, FALSE, kStartupMutexName);
    if (g_startupMutex == nullptr) {
        return false;
    }

    const DWORD wait_result = WaitForSingleObject(g_startupMutex, INFINITE);
    if (wait_result != WAIT_OBJECT_0 && wait_result != WAIT_ABANDONED) {
        CloseHandle(g_startupMutex);
        g_startupMutex = nullptr;
        return false;
    }

    return true;
}

void EndStartup() {
    if (g_startupMutex == nullptr) {
        return;
    }

    ReleaseMutex(g_startupMutex);
    CloseHandle(g_startupMutex);
    g_startupMutex = nullptr;
}

UINT QueryVersionMessage() {
    static const UINT message = RegisterWindowMessageW(kQueryVersionMessageName);
    return message;
}

UINT RetireWhenIdleMessage() {
    static const UINT message = RegisterWindowMessageW(kRetireMessageName);
    return message;
}

StartupResult ResolvePreviousInstances(HWND current_window, unsigned int current_version) {
    std::vector<ExistingInstance> instances;
    HostSearchContext search{};
    search.currentWindow = current_window;
    search.instances = &instances;
    EnumWindows(FindHostWindow, reinterpret_cast<LPARAM>(&search));

    for (ExistingInstance& instance : instances) {
        instance.version = QueryRunningVersion(instance.host);
        if (instance.version >= current_version && instance.version != 0) {
            return StartupResult::ExistingSameOrNewer;
        }
    }

    for (const ExistingInstance& instance : instances) {
        const bool retired = instance.version == 0
            ? RetireLegacyInstance(instance)
            : RetireProtocolInstance(instance);

        if (!retired) {
            return StartupResult::Failed;
        }
    }

    return StartupResult::Continue;
}

} // namespace InstanceHandoff
