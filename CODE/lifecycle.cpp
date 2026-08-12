#include "lifecycle.h"

namespace {
HWND g_hostWindow = nullptr;
unsigned int g_busyCount = 0;
bool g_retiring = false;
bool g_closePosted = false;

void TryCloseWhenIdle() {
    if (!g_retiring || g_busyCount != 0 || g_closePosted || g_hostWindow == nullptr) {
        return;
    }

    if (IsWindow(g_hostWindow) != FALSE && PostMessageW(g_hostWindow, WM_CLOSE, 0, 0) != FALSE) {
        g_closePosted = true;
    }
}
} // namespace

namespace Lifecycle {

void Initialize(HWND host_window) {
    g_hostWindow = host_window;
    g_busyCount = 0;
    g_retiring = false;
    g_closePosted = false;
}

void BeginActivity() {
    ++g_busyCount;
}

void EndActivity() {
    if (g_busyCount > 0) {
        --g_busyCount;
    }

    TryCloseWhenIdle();
}

void RequestRetire() {
    g_retiring = true;
    TryCloseWhenIdle();
}

bool IsRetiring() {
    return g_retiring;
}

void Shutdown() {
    g_hostWindow = nullptr;
    g_busyCount = 0;
    g_retiring = false;
    g_closePosted = false;
}

} // namespace Lifecycle
