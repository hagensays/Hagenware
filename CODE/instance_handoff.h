#pragma once

#include <windows.h>

namespace InstanceHandoff {

enum class StartupResult {
    Continue,
    ExistingSameOrNewer,
    Failed,
};

bool BeginStartup();
void EndStartup();
UINT QueryVersionMessage();
UINT RetireWhenIdleMessage();
StartupResult ResolvePreviousInstances(HWND current_window, unsigned int current_version);

} // namespace InstanceHandoff
